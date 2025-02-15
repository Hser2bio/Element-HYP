/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 */
#include "bip38tooldialog.h"
#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "charitydialog.h"
#include "calcdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "networkstyle.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "bitcoinrpc.h"
#include "blockbrowser.h"
#include "votingdialog.h"
#include "../wallet.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QFont>
#include <QFontDatabase>

#if QT_VERSION < 0x050000
#include <QUrl>
#endif
#include <QStyle>
#include <QMimeData>
#include <QSignalMapper>
#include <QSettings>
#include <iostream>

extern CWallet *pwalletMain;
extern int64 nLastCoinStakeSearchInterval;
extern unsigned int nStakeTargetSpacing;

BitcoinGUI::BitcoinGUI(const NetworkStyle * networkStyle, QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    encryptWalletAction(0),
	unlockWalletAction(0),
    changePassphraseAction(0),
    lockWalletToggleAction(0),
    bip38ToolAction(0),
    aboutQtAction(0),
    trayIcon(0),
    trayIconMenu(0),
    notificator(0),
    rpcConsole(0)
{
    setFixedSize(765,710);
    //Creating font directory and establishing global font preference
    QFontDatabase::addApplicationFont(":/fonts/Lato");
    QFont font("Lato", 10);
    QApplication::setFont(font);
    //QApplication::setStyleSheet ( const QString & sheet )

//    setMaximumSize(750,750);
//    resize(800, 800);
    
    QString windowTitle = tr("Element") + " " + tr("(HYP)") + " " + tr("Wallet");
    windowTitle += " " + networkStyle->getTitleAddText();
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
#endif
    setWindowTitle(windowTitle);
    
#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras.
    setUnifiedTitleAndToolBarOnMac(true);
#endif
    
    // Accept D&D of URIs
    setAcceptDrops(true);

    /* zeewolf: Hot swappable wallet themes */
    // Discover themes
    listThemes(themesList);
    /* /zeewolf: Hot swappable wallet themes */
    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create the tray icon (or setup the dock icon) 
    createTrayIcon(networkStyle);

    // Create tabs
    overviewPage = new OverviewPage();

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);
    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);
    sendCoinsPage = new SendCoinsDialog(this);
    signVerifyMessageDialog = new SignVerifyMessageDialog(this);
    bip38Dialog = new Bip38ToolDialog(this);
	stakeForCharityDialog = new StakeForCharityDialog(this);
	blockBrowser = new BlockBrowser((this));
    votingDialog = new VotingDialog(this);


	
    centralWidget = new QStackedWidget(this);
    centralWidget->addWidget(overviewPage);
    centralWidget->addWidget(transactionsPage);
    centralWidget->addWidget(addressBookPage);
    centralWidget->addWidget(receiveCoinsPage);
    centralWidget->addWidget(sendCoinsPage);
	centralWidget->addWidget(stakeForCharityDialog);
    setCentralWidget(centralWidget);

    
    
    // Create status bar
    statusBar();

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    labelEncryptionIcon = new GUIUtil::ClickableLabel();
    labelMintingIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelMintingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    // Set minting pixmap
    labelMintingIcon->setEnabled(false);

    // Add timer to update minting info
    QTimer *timerMintingIcon = new QTimer(labelMintingIcon);
    timerMintingIcon->start(MINTING_UPDATE_DELAY);
    connect(timerMintingIcon, SIGNAL(timeout()), this, SLOT(updateMintingIcon()));

    // Add timer to update minting weights
    QTimer *timerMintingWeights = new QTimer(labelMintingIcon);
    timerMintingWeights->start(1 * 5000); // 5 second update time
    connect(timerMintingWeights, SIGNAL(timeout()), this, SLOT(updateMintingWeights()));

    // Set initial values for user and network weights
    nWeight = 0;
	nHoursToMaturity = 0;
	nNetworkWeight = 0;
	nAmount = 0;

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel(); 
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = qApp->style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);
    statusBar()->setSizeGripEnabled(false);

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);
    miningIconMovie = new QMovie(":/movies/mining", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));

	// Clicking on stake for charity button in the address book sends you to the S4C page
    connect(addressBookPage, SIGNAL(stakeForCharitySignal(QString)), this, SLOT(charityClicked(QString)));
	
	// Clicking on "Block Browser" in the transaction page sends you to the blockbrowser
	connect(transactionView, SIGNAL(blockBrowserSignal(QString)), this, SLOT(gotoBlockBrowser(QString)));
	
    gotoOverviewPage();
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::instance()->setMainWindow(NULL);
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setToolTip(tr("Show general overview of wallet"));
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a Element address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Contacts"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);


    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
	

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/bitcoin"), tr("&About Element"), this);
    aboutAction->setToolTip(tr("Show information about Element"));
    aboutAction->setMenuRole(QAction::AboutRole);

    charityAction = new QAction(QIcon(":/icons/s4c"), tr("&MultiSend"), this);
    charityAction->setToolTip(tr("MultiSend Settings"));
    charityAction->setCheckable(true);
	charityAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
	tabGroup->addAction(charityAction);

	calcAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Stake Calculator"), this);
    calcAction->setToolTip(tr("Open Stake Calculator"));
    calcAction->setMenuRole(QAction::AboutRole);
	
	blockAction = new QAction(QIcon(":/icons/blexp"), tr("Block Bro&wser"), this);
	blockAction->setStatusTip(tr("Explore the BlockChain"));
	blockAction->setToolTip(blockAction->statusTip());

    votingAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Voting"), this);
    votingAction->setStatusTip(tr("Set and View Vote Proposals and Settings"));
    votingAction->setToolTip(votingAction->statusTip());

    aboutQtAction = new QAction(QIcon(":/icons/qtlogo-64"), tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for Element"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt wallet"));
    encryptWalletAction->setCheckable(true);
	unlockWalletAction = new QAction(QIcon(":/icons/lock_open"), tr("&Unlock Wallet For PoS..."), this);
	unlockWalletAction->setStatusTip(tr("Unlock the wallet for PoS"));
	unlockWalletAction->setCheckable(true);
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setToolTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setToolTip(tr("Change the passphrase used for wallet encryption"));
    lockWalletToggleAction = new QAction(this);
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);
    bip38ToolAction = new QAction(QIcon(":/icons/key"), tr("&BIP38 tool"), this);
    bip38ToolAction->setToolTip(tr("Encrypt and decrypt private keys using a passphrase"));
	
	checkWalletAction = new QAction(QIcon(":/icons/transaction_confirmed"), tr("&Check Wallet..."), this);
	checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));
	
	repairWalletAction = new QAction(QIcon(":/icons/options"), tr("&Repair Wallet..."), this);
	repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));
	
    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug"), this);
    openRPCConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
	
	connect(charityAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
	connect(charityAction, SIGNAL(triggered()), this, SLOT(charityClicked()));
	
	connect(calcAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
	connect(calcAction, SIGNAL(triggered()), this, SLOT(calcClicked()));
	
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
	connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
	connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(lockWalletToggleAction, SIGNAL(triggered()), this, SLOT(lockWalletToggle()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
    connect(bip38ToolAction, SIGNAL(triggered()), this, SLOT(gotoBip38Tool()));
	connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWalletForMint()));
	
	connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));
    connect(votingAction, SIGNAL(triggered()), this, SLOT(gotoVotingDialog()));

    /* zeewolf: Hot swappable wallet themes */
    if (themesList.count()>0)
    {
        QSignalMapper* signalMapper = new QSignalMapper (this) ;
        //QActionGroup* menuActionGroup = new QActionGroup( this );
        //menuActionGroup->setExclusive(true);

        // Add custom themes (themes directory)
        for( int i=0; i < themesList.count(); i++ )
        {
            QString theme=themesList[i];
            customActions[i] = new QAction(QIcon(":/icons/options"), theme, this);
            customActions[i]->setToolTip(QString("Switch to " + theme + " theme"));
            customActions[i]->setStatusTip(QString("Switch to " + theme + " theme"));
            //customActions[i]->setActionGroup(menuActionGroup);
            signalMapper->setMapping(customActions[i], theme);
            connect(customActions[i], SIGNAL(triggered()), signalMapper, SLOT (map()));
        }
        connect(signalMapper, SIGNAL(mapped(QString)), this, SLOT(changeTheme(QString)));
    }
    /* /zeewolf: Hot swappable wallet themes */
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addSeparator();
    file->addAction(quitAction);
	
	QMenu *network = appMenuBar->addMenu(tr("&Network"));
	network->addAction(blockAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Tools"));
    settings->addAction(encryptWalletAction);
	settings->addAction(lockWalletToggleAction);
	settings->addAction(unlockWalletAction);
    settings->addAction(changePassphraseAction);
	settings->addAction(checkWalletAction);
	settings->addAction(repairWalletAction);
	settings->addAction(charityAction);
	settings->addAction(calcAction);
    settings->addAction(bip38ToolAction);
    if (fTestNet)
        settings->addAction(votingAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    /* zeewolf: Hot swappable wallet themes */
    if (themesList.count()>0)
    {
        QMenu *themes = appMenuBar->addMenu(tr("T&hemes"));
        for (int i = 0; i < themesList.count(); i++) {
            themes->addAction(customActions[i]);
        }
    }
    /* /zeewolf: Hot swappable wallet themes */

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
    QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
    toolbar->setObjectName("toolbar");
    toolbar->setMovable( false );
    toolbar->setStyleSheet("#toolbar { padding-left: 10px; border:0px; height:100%; text-align: center;} QToolBar QToolButton { margin: 0px; min-width:146px; max-width:146px; min-height:30px; max-height:30px; text-align: center; }");
    //toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    
    //below replaces the icons and text menu labels for text only
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(overviewAction);
    toolbar->addAction(sendCoinsAction);
    toolbar->addAction(receiveCoinsAction);
    toolbar->addAction(historyAction);
    toolbar->addAction(addressBookAction);



/*	toolbar->addAction(charityAction);

    QToolBar *toolbar2 = addToolBar(tr("Actions toolbar"));
    toolbar2->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toolbar2->addAction(openRPCConsoleAction);
    toolbar2->addAction(lockWalletToggleAction);
    toolbar2->addAction(exportAction);
*/
}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        createTrayIconMenu();
        
        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(setNumBlocks(int,int)));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        rpcConsole->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());
    }
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Report errors from wallet thread
        connect(walletModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);

        overviewPage->setModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);
        bip38Dialog->setModel(walletModel);
		stakeForCharityDialog->setModel(walletModel);
        votingDialog->SetWalletModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void BitcoinGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr("Element client") + " " + networkStyle->getTitleAddText();
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->show();
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon, this);
}


void BitcoinGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-Mac OSes)
    if (!trayIcon)
        return;
    
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif
    
    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}


#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::lockIconClicked()
{
    if(!walletModel)
        return;

    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
        unlockWalletForMint();
}

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to Element (HYP) network", "", count));
}

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel || clientModel->getNumConnections() == 0)
    {
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

        return;
    }

    QString strStatusBarWarnings = clientModel->getStatusBarWarnings();
    QString tooltip;

    if(count < nTotalBlocks)
    {
        int nRemainingBlocks = nTotalBlocks - count;
        float nPercentageDone = count / (nTotalBlocks * 0.01f);

        if (strStatusBarWarnings.isEmpty())
        {
            progressBarLabel->setText(tr(" Synchronizing..."));
            progressBarLabel->setVisible(true);
            progressBar->setFormat(tr("~%n block(s) remaining", "", nRemainingBlocks));
            progressBar->setMaximum(nTotalBlocks);
            progressBar->setValue(count);
            progressBar->setVisible(true);
            progressBar->setStyleSheet("QProgressBar { background-color: grey; border: 0px solid grey; border-radius: 0px; padding: 0px; text-align: center; color: white; width: 550px; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #3ddc97, stop: 1 #1351d8); border-radius: 0px; margin: 0px; }");
        }

        tooltip = tr("Downloaded %1 of %2 blocks of transaction history (%3% done).").arg(count).arg(nTotalBlocks).arg(nPercentageDone, 0, 'f', 2);
    }
    else
    {
        if (strStatusBarWarnings.isEmpty())

            //progressBarLabel->setText(tr(" Crypto-city.com"));
            progressBarLabel->setPixmap(QIcon(":/images/ccicon").pixmap(147,32));
            progressBarLabel->setVisible(true);
            //progressBarLabel->setStyleSheet("QLabel { background-image: url(:images/ccicon.png); border: 0px solid grey; border-radius: 0px; padding: 0px; text-align: center; color: blue; width: 200px; margin-right: 5px; }");


        progressBar->setVisible(false);
        tooltip = tr("Downloaded %1 blocks of transaction history.").arg(count);
    }

    // Override progressBarLabel text and hide progress bar, when we have warnings to display
    if (!strStatusBarWarnings.isEmpty())
    {
        progressBarLabel->setText(strStatusBarWarnings);
        progressBarLabel->setVisible(true);
        progressBar->setVisible(false);
    }

	tooltip = tr("Current difficulty is %1.").arg(clientModel->GetDifficulty()) + QString("<br>") + tooltip;

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    int secs = lastBlockDate.secsTo(QDateTime::currentDateTime());
    QString text;

    // Represent time from last generated block in human readable text
    if(secs <= 0)
    {
        // Fully up to date. Leave text empty.
    }
    else if(secs < 60)
    {
        text = tr("%n second(s) ago","",secs);
    }
    else if(secs < 60*60)
    {
        text = tr("%n minute(s) ago","",secs/60);
    }
    else if(secs < 24*60*60)
    {
        text = tr("%n hour(s) ago","",secs/(60*60));
    }
    else
    {
        text = tr("%n day(s) ago","",secs/(60*60*24));
    }

    // Set icon state: spinning if catching up, tick otherwise
    if(count >= nTotalBlocks) //completely remove time requirement
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        overviewPage->showOutOfSyncWarning(false);
    }
    else
    {
        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        syncIconMovie->start();

        overviewPage->showOutOfSyncWarning(true);
    }

    if(!text.isEmpty())
    {
        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1.").arg(text);
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if(modal)
    {
        QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
    } else {
        notificator->notify(Notificator::Critical, title, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    QString strMessage =
        tr("This transaction is over the size limit.  You can still send it for a fee of %1, "
          "which goes to the nodes that process your transaction and helps to support the network.  "
          "Do you want to pay the fee?").arg(
                BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    if(!clientModel->inInitialBlockDownload())
    {
		fMultiSendNotify = pwalletMain->fMultiSendNotify;
		
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                        .data().toString();
        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                        .data().toString();
        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();
        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                            TransactionTableModel::ToAddress, parent)
                        .data(Qt::DecorationRole));

        notificator->notify(Notificator::Information,
                            (amount)<0 ? (fMultiSendNotify == true ? tr("Sent MultiSend transaction") : tr("Sent transaction") ):
                                         tr("Incoming transaction"),
                              tr("Date: %1\n"
                                 "Amount: %2\n"
                                 "Type: %3\n"
                                 "Address: %4\n")
                              .arg(date)
                              .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                              .arg(type)
                              .arg(address), icon);
			
		pwalletMain->fMultiSendNotify = false;
		
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    centralWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    centralWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoBlockBrowser(QString transactionId)
{
	if(!transactionId.isEmpty())
		blockBrowser->setTransactionId(transactionId);
	
	blockBrowser->show();
}

void BitcoinGUI::gotoVotingDialog()
{
    votingDialog->show();
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}


void BitcoinGUI::gotoBip38Tool()
{
    bip38Dialog->showTab_ENC(true);
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (sendCoinsPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Element address or malformed URI parameters."));
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Element address or malformed URI parameters."));
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        encryptWalletAction->setEnabled(true);
		labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
		disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        changePassphraseAction->setEnabled(false);
        lockWalletToggleAction->setVisible(false);
		unlockWalletAction->setChecked(false);
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
		unlockWalletAction->setChecked(false);
		unlockWalletAction->setEnabled(false);
        changePassphraseAction->setEnabled(true);
        lockWalletToggleAction->setVisible(true);
        lockWalletToggleAction->setIcon(QIcon(":/icons/lock_closed"));
        lockWalletToggleAction->setText(tr("&Lock Wallet"));
        lockWalletToggleAction->setToolTip(tr("Lock wallet"));
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
		unlockWalletAction->setChecked(false);
		unlockWalletAction->setEnabled(true);
        changePassphraseAction->setEnabled(true);
        lockWalletToggleAction->setVisible(true);
        lockWalletToggleAction->setIcon(QIcon(":/icons/lock_open"));
        lockWalletToggleAction->setText(tr("&Unlock Wallet..."));
        lockWalletToggleAction->setToolTip(tr("Unlock wallet"));
		disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        break;
    }
}

void BitcoinGUI::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void BitcoinGUI::checkWallet()
{

    int nMismatchSpent;
    int64 nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Check the wallet as requested by user
    walletModel->checkWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
        notificator->notify(Notificator::Warning,
		tr("Check Wallet Information"),
                tr("Wallet passed integrity test!\n"
                   "Nothing found to fix."));
  else
		notificator->notify(Notificator::Warning, 
			tr("Check Wallet Information"), tr("Wallet failed integrity test!\n\n"
                  "Mismatched coin(s) found: %1.\n"
                  "Amount in question: %2.\n"
                  "Orphans found: %3.\n\n"
                  "Please backup wallet and run repair wallet.\n")
						.arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound));
}

void BitcoinGUI::repairWallet()
{
    int nMismatchSpent;
    int64 nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Repair the wallet as requested by user
    walletModel->repairWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
       notificator->notify(Notificator::Warning,
	   tr("Repair Wallet Information"),
               tr("Wallet passed integrity test!\n"
                  "Nothing found to fix."));
    else
		notificator->notify(Notificator::Warning,
		tr("Repair Wallet Information"),
               tr("Wallet failed integrity test and has been repaired!\n"
                  "Mismatched coin(s) found: %1\n"
                  "Amount affected by repair: %2\n"
                  "Orphans removed: %3\n")
                        .arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound));
}

void BitcoinGUI::backupWallet()
{
	#if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
	#else
	QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	#endif
	
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void BitcoinGUI::lockWalletToggle()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
		AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
    else
        walletModel->setWalletLocked(true);
}

void BitcoinGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void BitcoinGUI::unlockWalletForMint()
{
    if(!walletModel)
        return;

    // Unlock wallet when requested by user
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::UnlockForMint, this);
        dlg.setModel(walletModel);
        dlg.exec();
		
		// Only show message if unlock is sucessfull.
		if(walletModel->getEncryptionStatus() == WalletModel::Unlocked)
		notificator->notify(Notificator::Warning,
			tr("Unlock Wallet Information"),
                tr("Wallet has been unlocked. \n"
					"Proof of Stake has started.\n"));
    }
}

void BitcoinGUI::lockWallet()
{
    if(!walletModel)
       return;

    // Lock wallet when requested by user
    if(walletModel->getEncryptionStatus() == WalletModel::Unlocked)
         walletModel->setWalletLocked(true,"",true);
	notificator->notify(Notificator::Warning,
			tr("Lock Wallet Information"),
                tr("Wallet has been unlocked. \n"
					"Proof of Stake has stopped.\n"));

}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateMintingIcon()
{
    if (pwalletMain && pwalletMain->IsLocked())
    {
        labelMintingIcon->setToolTip(tr("Not minting because wallet is locked.<br>Network weight is %1.<br>MultiSend: %2").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Not Active")));
        labelMintingIcon->setEnabled(false);
        labelMintingIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
	else if (pwalletMain && pwalletMain->fDisableStake)
    {
        labelMintingIcon->setToolTip(tr("Not minting because staking is disabled.<br>Network weight is %1.<br>MultiSend: %2").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Not Active")));
        labelMintingIcon->setEnabled(false);
        labelMintingIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
    else if (vNodes.empty())
    {
        labelMintingIcon->setToolTip(tr("Not minting because wallet is offline.<br>Network weight is %1.<br>MultiSend: %2").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Not Active")));
        labelMintingIcon->setEnabled(false);
        labelMintingIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
    else if (IsInitialBlockDownload())
    {
        labelMintingIcon->setToolTip(tr("Not minting because wallet is syncing.<br>Network weight is %1.<br>MultiSend: %2").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Not Active")));
        labelMintingIcon->setEnabled(false);
        labelMintingIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
    else if (!nWeight)
    {
        labelMintingIcon->setToolTip(tr("Not minting because you don't have mature coins.<br>Next block matures in %2 hours<br>Network weight is %1<br>MultiSend: %3").arg(nNetworkWeight).arg(nHoursToMaturity).arg(fMultiSend ? tr("Active"):tr("Not Active")));
        labelMintingIcon->setEnabled(false);
        labelMintingIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
    else if (nLastCoinStakeSearchInterval)
    {	
		uint64 nAccuracyAdjustment = 1; // this is a manual adjustment param if needed to make more accurate
        uint64 nEstimateTime = 60 * nNetworkWeight / nWeight / nAccuracyAdjustment;
	
		uint64 nRangeLow = nEstimateTime;
		uint64 nRangeHigh = nEstimateTime * 1.5;
        QString text;
        if (nEstimateTime < 60)
        {
            text = tr("%1 - %2 seconds").arg(nRangeLow).arg(nRangeHigh);
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%1 - %2 minutes").arg(nRangeLow / 60).arg(nRangeHigh / 60);
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%1 - %2 hours").arg(nRangeLow / (60*60)).arg(nRangeHigh / (60*60));
        }
        else
        {
            text = tr("%1 - %2 days").arg(nRangeLow / (60*60*24)).arg(nRangeHigh / (60*60*24));
        }

        labelMintingIcon->setMovie(miningIconMovie);
        miningIconMovie->start();
        labelMintingIcon->setEnabled(true);
        labelMintingIcon->setToolTip(tr("Minting.<br>Your weight is %1.<br>Network weight is %2.<br><b>Estimated</b> next stake in %3.<br>MultiSend: %4").arg(nWeight).arg(nNetworkWeight).arg(text).arg(fMultiSend ? tr("Active"):tr("Not Active")));
    }
    else
    {
        labelMintingIcon->setToolTip(tr("Not minting."));
        labelMintingIcon->setEnabled(false);
        labelMintingIcon->setPixmap(QIcon(":/icons/mining_inactive").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    }
}

uint nLastWeightCheck = 0;
void BitcoinGUI::updateMintingWeights()
{
    // Only update if we have the network's current number of blocks, or weight(s) are zero (fixes lagging GUI)
    if ((clientModel && clientModel->getNumBlocks() >= clientModel->getNumBlocksOfPeers()) || !nWeight || !nNetworkWeight)
    {
        //only update weight every 120 seconds in order to reduce resource consumption
	    if(GetTime() - nLastWeightCheck > 120)
	    {
            nWeight = 0;
            nAmount = 0;
            nNetworkWeight = GetPoSKernelPS();
            nLastWeightCheck = GetTime();

            if(!pwalletMain)
                return;

            if(pwalletMain->MintableCoins())
                nHoursToMaturity = 0;
            else
                nHoursToMaturity = pwalletMain->GetTimeToNextMaturity() / (60*60);

            if(pwalletMain->bnStakeWeightCached != 0)
            {
                nHoursToMaturity = 0;
                nWeight = pwalletMain->bnStakeWeightCached.getuint64();
            }
            else
                pwalletMain->GetStakeWeight(*pwalletMain, nMinMax, nMinMax, nWeight, nAmount);
	   }
    }
	
	//MultiSend check
	if(walletModel)
		fMultiSend = pwalletMain->fMultiSend;
}

void BitcoinGUI::charityClicked(QString addr)
{
    charityAction->setChecked(true);
    centralWidget->setCurrentWidget(stakeForCharityDialog);

    if(!addr.isEmpty())
        stakeForCharityDialog->setAddress(addr);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::calcClicked()
{
    calcDialog dlg;
    dlg.exec();
}

/* zeewolf: Hot swappable wallet themes */
void BitcoinGUI::changeTheme(QString theme)
{
    // load Default theme first (if present) to apply default styles
    loadTheme("Default");

    if (theme != "Default") {
        loadTheme(theme);
    }
}

void BitcoinGUI::loadTheme(QString theme)
{
    // template variables : key => value
    QMap<QString, QString> variables;

    // path to selected theme dir - for simpler use, just use $theme-dir in qss : url($theme-dir/image.png)
    QString themeDir = themesDir + "/" + theme;

    // if theme selected
    if (theme != "") {
        QFile qss(themeDir + "/styles.qss");
        // open qss
        if (qss.open(QFile::ReadOnly))
        {
            // read stylesheet
            QString styleSheet = QString(qss.readAll());
            QTextStream in(&qss);
            // rewind
            in.seek(0);
            bool readingVariables = false;

            // seek for variables
            while(!in.atEnd()) {
                QString line = in.readLine();
                // variables starts here
                if (line == "/** [VARS]") {
                    readingVariables = true;
                }
                // variables end here
                if (line == "[/VARS] */") {
                    break;
                }
                // if we're reading variables - store them in a map
                if (readingVariables == true) {
                    // skip empty lines
                    if (line.length()>3 && line.contains('=')) {
                        QStringList fields = line.split("=");
                        QString var = fields.at(0).trimmed();
                        QString value = fields.at(1).trimmed();
                        variables[var] = value;
                    }
                }
            }

            // replace path to themes dir
            styleSheet.replace("$theme-dir", themeDir);
            styleSheet.replace("$themes-dir", themesDir);

            QMapIterator<QString, QString> variable(variables);
            variable.toBack();
            // iterate backwards to prevent overwriting variables
            while (variable.hasPrevious()) {
                variable.previous();
                // replace variables
                styleSheet.replace(variable.key(), variable.value());
            }

            qss.close();

            // Apply the result qss file to Qt

            /*if (styleSheet.contains("$", Qt::CaseInsensitive)) {
                QRegExp rx("(\\$[-\\w]+)");
                rx.indexIn(styleSheet);
                QString captured = rx.cap(1);
                QMessageBox::warning(this, "Theme syntax error", "You have used variable that is not declared " + captured + ". Theme will not be applied.");
            } else {*/
                qApp->setStyleSheet(styleSheet);
            /*}*/
        }
    } else {
        // If not theme name given - clear styles
        qApp->setStyleSheet(QString(""));
    }

    // set selected theme and store it in registry
    selectedTheme = theme;
    QSettings settings;
    settings.setValue("Template", selectedTheme);
}

void BitcoinGUI::listThemes(QStringList& themes)
{
    QDir currentDir(qApp->applicationDirPath());
    // try app dir
    if (currentDir.cd("themes")) {
    // got it! (win package)
    } else if (currentDir.cd("src/qt/res/themes")) {
        // got it
    } else if (currentDir.cd("../src/qt/res/themes")) {
        // got it
    } else {
        // themes not found :(
        return;
    }
    themesDir = currentDir.path();
    currentDir.setFilter(QDir::Dirs);
    QStringList entries = currentDir.entryList();
    for( QStringList::ConstIterator entry=entries.begin(); entry!=entries.end(); ++entry )
    {
        QString themeName=*entry;
        if(themeName != tr(".") && themeName != tr(".."))
        {
            themes.append(themeName);
        }
    }

    // get selected theme from registry (if any)
    QSettings settings;
    selectedTheme = settings.value("Template").toString();
    // or use default theme - HyperBlue
    if (selectedTheme=="") {
        selectedTheme = "HyperBlue";
    }
    // load it!
    loadTheme(selectedTheme);
}

void BitcoinGUI::keyPressEvent(QKeyEvent * e)
{
    switch (e->type())
     {
       case QEvent::KeyPress:
         // $ key
         if (e->key() == 36) {
             // dev feature: key reloads selected theme
             loadTheme(selectedTheme);
         }
         break;
       default:
         break;
     }

}

/* /zeewolf: Hot swappable wallet themes */
