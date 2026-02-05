#include "loginwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QIcon>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QRandomGenerator>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QOverload>
#include <QScreen>
#include <windows.h>
#include <winuser.h>

LoginWindow *LoginWindow::s_instance = nullptr;

LRESULT CALLBACK LoginWindow::keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    qDebug() << "keyboardHookProc called, nCode:" << nCode << "wParam:" << wParam;
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT *kbdStruct = (KBDLLHOOKSTRUCT*) lParam;
        if (wParam == WM_KEYDOWN) {
            qDebug() << "Global keyboard hook: key down, vkCode:" << kbdStruct->vkCode;
            
            if (s_instance && s_instance->m_rdpProcess && 
                s_instance->m_rdpProcess->state() == QProcess::Running) {
                
                QByteArray keyData;
                BYTE keyState[256];
                GetKeyboardState(keyState);
                
                if (keyState[VK_CONTROL] & 0x80) {
                    keyData.append("\x1D");
                }
                if (keyState[VK_MENU] & 0x80) {
                    keyData.append("\x38");
                }
                if (keyState[VK_SHIFT] & 0x80) {
                    keyData.append("\x2A");
                }
                
                switch (kbdStruct->vkCode) {
                    case VK_RETURN:
                        keyData.append("\x1C\x0D");
                        break;
                    case VK_ESCAPE:
                        keyData.append("\x01");
                        break;
                    case VK_TAB:
                        keyData.append("\x0F");
                        break;
                    case VK_BACK:
                        keyData.append("\x0E");
                        break;
                    case VK_DELETE:
                        keyData.append("\x53");
                        break;
                    case VK_UP:
                        keyData.append("\x48");
                        break;
                    case VK_DOWN:
                        keyData.append("\x50");
                        break;
                    case VK_LEFT:
                        keyData.append("\x4B");
                        break;
                    case VK_RIGHT:
                        keyData.append("\x4D");
                        break;
                    case VK_F1:
                        keyData.append("\x3B");
                        break;
                    case VK_F2:
                        keyData.append("\x3C");
                        break;
                    case VK_F3:
                        keyData.append("\x3D");
                        break;
                    case VK_F4:
                        keyData.append("\x3E");
                        break;
                    case VK_F5:
                        keyData.append("\x3F");
                        break;
                    case VK_F6:
                        keyData.append("\x40");
                        break;
                    case VK_F7:
                        keyData.append("\x41");
                        break;
                    case VK_F8:
                        keyData.append("\x42");
                        break;
                    case VK_F9:
                        keyData.append("\x43");
                        break;
                    case VK_F10:
                        keyData.append("\x44");
                        break;
                    case VK_F11:
                        keyData.append("\x57");
                        break;
                    case VK_F12:
                        keyData.append("\x58");
                        break;
                    default:
                        if (kbdStruct->vkCode >= 'A' && kbdStruct->vkCode <= 'Z') {
                            keyData.append(kbdStruct->vkCode);
                        } else if (kbdStruct->vkCode >= '0' && kbdStruct->vkCode <= '9') {
                            keyData.append(kbdStruct->vkCode);
                        }
                        break;
                }
                
                if (!keyData.isEmpty()) {
                    qDebug() << "Sending key data to RDP:" << keyData.toHex();
                    s_instance->m_rdpProcess->write(keyData);
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// 构造函数：初始化登录窗口
// 参数：parent - 父窗口指针
LoginWindow::LoginWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_serverEdit(nullptr)
    , m_usernameEdit(nullptr)
    , m_passwordEdit(nullptr)
    , m_rememberPasswordCheckBox(nullptr)
    , m_autoLoginCheckBox(nullptr)
    , m_languageButton(nullptr)
    , m_languageMenu(nullptr)
    , m_loginButton(nullptr)
    , m_statusLabel(nullptr)
    , m_networkManager(nullptr)
    , m_titleLabel(nullptr)
    , m_vmListWidget(nullptr)
    , m_vmListContainer(nullptr)
    , m_backButton(nullptr)
    , m_refreshButton(nullptr)
    , m_vmTitleLabel(nullptr)
    , m_vmStatusLabel(nullptr)
    , m_stackedWidget(nullptr)
    , m_currentLanguage("en_US")
    , m_rdpProcess(nullptr)
    , m_disconnectButton(nullptr)
    , m_disconnectButtonDragging(false)
    , m_disconnectButtonActuallyDragged(false)
    , m_disconnectButtonDragPosition(0, 0)
    , m_disconnectButtonPressPosition(0, 0)
    , m_disconnectButtonDocked(false)
    , m_disconnectButtonDockedPosition(0, 0)
    , m_randomGenerator()
    , m_keyboardHook(nullptr)
{
    qDebug() << "LoginWindow constructor called";
    s_instance = this;
    qDebug() << "Setting s_instance to:" << s_instance;
    qDebug() << "About to install keyboard hook...";
    m_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHookProc, GetModuleHandle(NULL), 0);
    if (m_keyboardHook == NULL) {
        qDebug() << "Failed to install keyboard hook, error:" << GetLastError();
    } else {
        qDebug() << "Keyboard hook installed successfully";
    }
    // 初始化多语言翻译字典
    initTranslations();
    
    // 配置SSL，禁用证书验证（用于开发环境）
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    QSslConfiguration::setDefaultConfiguration(sslConfig);

    // 创建网络访问管理器，用于处理HTTP请求
    m_networkManager = new QNetworkAccessManager(this);

    // 设置用户界面
    setupUi();
    
    // 加载保存的设置
    loadSettings();
    
    // 检查是否需要自动登录
    if (m_autoLoginCheckBox->isChecked() && 
        !m_serverEdit->text().trimmed().isEmpty() && 
        !m_usernameEdit->text().trimmed().isEmpty() && 
        !m_passwordEdit->text().isEmpty()) {
        // 延迟一下再自动登录，确保UI完全加载
        QTimer::singleShot(500, this, [this]() {
            onLoginClicked();
        });
    }
    
    QCoreApplication::instance()->installEventFilter(this);
}

// 析构函数：清理资源
LoginWindow::~LoginWindow()
{
    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
        qDebug() << "Keyboard hook uninstalled";
    }
    
    if (m_rdpProcess) {
        if (m_rdpProcess->state() == QProcess::Running) {
            m_rdpProcess->terminate();
            if (!m_rdpProcess->waitForFinished(3000)) {
                m_rdpProcess->kill();
            }
        }
        delete m_rdpProcess;
        m_rdpProcess = nullptr;
    }
}

// 设置主窗口用户界面
// 使用QStackedWidget实现登录页面和虚拟机列表页面的切换
void LoginWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    centralWidget->setStyleSheet(
        "QWidget { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #dbeafe, stop:1 #93c5fd); }"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_stackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(m_stackedWidget);

    QWidget *loginWidget = new QWidget();
    setupLoginUi(loginWidget);
    m_stackedWidget->addWidget(loginWidget);

    QWidget *vmListWidget = new QWidget();
    setupVmListUi(vmListWidget);
    m_stackedWidget->addWidget(vmListWidget);

    m_stackedWidget->setCurrentIndex(0);

    m_disconnectButton = new QToolButton(nullptr);
    m_disconnectButton->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    m_disconnectButton->setAttribute(Qt::WA_TranslucentBackground);
    m_disconnectButton->setText(translate("disconnect"));
    m_disconnectButton->setFixedSize(120, 50);
    int visibleWidth = 6 + (m_randomGenerator.bounded(0, 5));
    m_disconnectButtonDockedPosition = QPoint(0, 10);
    m_disconnectButton->move(-m_disconnectButton->width() + visibleWidth, 10);
    m_disconnectButtonDocked = true;
    m_disconnectButton->setStyleSheet(
        "QToolButton {"
        "   background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #dbeafe, stop:1 #c4e0fa);"
        "   color: #6b7280;"
        "   border: none;"
        "   border-radius: 8px;"
        "   font-size: 16px;"
        "   font-weight: bold;"
        "   padding: 12px;"
        "}"
        "QToolButton:hover {"
        "   background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #c4e0fa, stop:1 #b3d5fc);"
        "}"
        "QToolButton:pressed {"
        "   background-color: #b3d5fc;"
        "}"
    );
    m_disconnectButton->hide();
    m_disconnectButton->installEventFilter(this);

    setWindowTitle("VDI Client");
    setWindowIcon(QIcon(":/logo.png"));
    resize(600, 650);
    setMinimumSize(480, 560);
}

// 设置登录页面用户界面
// 参数：widget - 登录页面的父控件
// 功能：创建服务器地址、用户名、密码输入框，以及记住密码、自动登录选项和语言切换按钮
void LoginWindow::setupLoginUi(QWidget *widget)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(widget);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *containerWidget = new QWidget();
    containerWidget->setStyleSheet(
        "QWidget {"
        "   background-color: #f5f7fa;"
        "   border-radius: 12px;"
        "}"
    );
    containerWidget->setMaximumWidth(420);

    QVBoxLayout *containerLayout = new QVBoxLayout(containerWidget);
    containerLayout->setSpacing(25);
    containerLayout->setContentsMargins(40, 40, 40, 40);

    m_titleLabel = new QLabel(translate("title"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        "font-size: 24px;"
        "font-weight: bold;"
        "color: #2c3e50;"
        "padding: 10px 0;"
    );
    containerLayout->addWidget(m_titleLabel);

    QString inputStyle = (
        "QLineEdit {"
        "   background-color: white;"
        "   border: 2px solid #e1e8ed;"
        "   border-radius: 8px;"
        "   padding: 12px 15px;"
        "   font-size: 14px;"
        "   color: #4a5568;"
        "}"
        "QLineEdit:focus {"
        "   border: 2px solid #3b82f6;"
        "   background-color: white;"
        "}"
    );

    m_serverEdit = new QLineEdit();
    m_serverEdit->setPlaceholderText(translate("server"));
    m_serverEdit->setMinimumHeight(45);
    m_serverEdit->setStyleSheet(inputStyle);
    containerLayout->addWidget(m_serverEdit);

    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText(translate("username"));
    m_usernameEdit->setMinimumHeight(45);
    m_usernameEdit->setStyleSheet(inputStyle);
    containerLayout->addWidget(m_usernameEdit);

    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setPlaceholderText(translate("password"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setMinimumHeight(45);
    m_passwordEdit->setStyleSheet(inputStyle);
    containerLayout->addWidget(m_passwordEdit);

    QHBoxLayout *optionsLayout = new QHBoxLayout();
    optionsLayout->setSpacing(15);

    QString checkboxStyle = (
        "QCheckBox {"
        "   font-size: 13px;"
        "   color: #64748b;"
        "   spacing: 5px;"
        "}"
        "QCheckBox::indicator {"
        "   width: 18px;"
        "   height: 18px;"
        "   border: 2px solid #cbd5e1;"
        "   border-radius: 4px;"
        "   background-color: white;"
        "}"
        "QCheckBox::indicator:checked {"
        "   background-color: #3b82f6;"
        "   border-color: #3b82f6;"
        "}"
        "QCheckBox::indicator:checked:hover {"
        "   background-color: #2563eb;"
        "}"
    );

    m_rememberPasswordCheckBox = new QCheckBox(translate("remember_password"));
    m_rememberPasswordCheckBox->setStyleSheet(checkboxStyle);
    optionsLayout->addWidget(m_rememberPasswordCheckBox);

    m_autoLoginCheckBox = new QCheckBox(translate("auto_login"));
    m_autoLoginCheckBox->setStyleSheet(checkboxStyle);
    connect(m_autoLoginCheckBox, &QCheckBox::checkStateChanged, this, &LoginWindow::onAutoLoginChanged);
    optionsLayout->addWidget(m_autoLoginCheckBox);

    optionsLayout->addStretch();

    m_languageButton = new QPushButton("🌐");
    m_languageButton->setMinimumSize(45, 38);
    m_languageButton->setMaximumSize(45, 38);
    m_languageButton->setStyleSheet(
        "QPushButton {"
        "   background: transparent;"
        "   border: none;"
        "   font-size: 20px;"
        "   padding: 6px 0px;"
        "   qproperty-alignment: AlignCenter;"
        "}"
        "QPushButton::menu-indicator {"
        "   image: none;"
        "}"
        "QPushButton:hover {"
        "   background: transparent;"
        "}"
        "QPushButton:pressed {"
        "   background: transparent;"
        "}"
    );
    connect(m_languageButton, &QPushButton::clicked, this, &LoginWindow::onLanguageButtonClicked);
    optionsLayout->addWidget(m_languageButton);

    m_languageMenu = new QMenu(this);
    m_languageMenu->setStyleSheet(
        "QMenu {"
        "   background-color: white;"
        "   border: 1px solid #e1e8ed;"
        "   border-radius: 8px;"
        "   padding: 5px;"
        "}"
        "QMenu::item {"
        "   padding: 8px 20px;"
        "   border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "   background-color: #3b82f6;"
        "   color: white;"
        "}"
    );

    QAction *englishAction = m_languageMenu->addAction("English");
    englishAction->setData("en_US");

    QAction *chineseAction = m_languageMenu->addAction("中文");
    chineseAction->setData("zh_CN");

    QAction *traditionalChineseAction = m_languageMenu->addAction("繁體中文");
    traditionalChineseAction->setData("zh_TW");

    QAction *japaneseAction = m_languageMenu->addAction("日本語");
    japaneseAction->setData("ja_JP");

    connect(m_languageMenu, &QMenu::triggered, this, &LoginWindow::onLanguageSelected);

    m_languageButton->setMenu(m_languageMenu);

    containerLayout->addLayout(optionsLayout);

    m_loginButton = new QPushButton(translate("login"));
    m_loginButton->setMinimumHeight(50);
    m_loginButton->setStyleSheet(
        "QPushButton {"
        "   background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #3b82f6, stop:1 #2563eb);"
        "   color: white;"
        "   border: none;"
        "   border-radius: 8px;"
        "   font-size: 16px;"
        "   font-weight: bold;"
        "   padding: 12px;"
        "}"
        "QPushButton:hover {"
        "   background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2563eb, stop:1 #1d4ed8);"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1e40af;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #cbd5e1;"
        "   color: #94a3b8;"
        "}"
    );
    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    containerLayout->addWidget(m_loginButton);

    m_statusLabel = new QLabel();
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(
        "font-size: 13px;"
        "color: #dc3545;"
        "padding: 5px;"
    );
    m_statusLabel->hide();
    containerLayout->addWidget(m_statusLabel);

    containerLayout->addStretch();

    mainLayout->addStretch();
    mainLayout->addWidget(containerWidget, 0, Qt::AlignCenter);
    mainLayout->addStretch();
}

// 设置虚拟机列表页面用户界面
// 参数参数：widget - 虚拟机列表页面的父控件
// 功能：创建虚拟机列表容器、返回按钮、刷新按钮和状态标签
void LoginWindow::setupVmListUi(QWidget *widget)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(widget);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    widget->setStyleSheet(
        "QWidget { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #dbeafe, stop:1 #93c5fd); }"
    );

    QWidget *containerWidget = new QWidget();
    containerWidget->setStyleSheet(
        "QWidget {"
        "   background-color: #f5f7fa;"
        "   border-radius: 12px;"
        "}"
    );
    containerWidget->setMaximumWidth(800);

    QVBoxLayout *containerLayout = new QVBoxLayout(containerWidget);
    containerLayout->setSpacing(25);
    containerLayout->setContentsMargins(40, 40, 40, 40);

    m_vmTitleLabel = new QLabel(translate("vm_list_title"));
    m_vmTitleLabel->setAlignment(Qt::AlignCenter);
    m_vmTitleLabel->setStyleSheet(
        "font-size: 24px;"
        "font-weight: bold;"
        "color: #2c3e50;"
        "padding: 10px 0;"
    );
    containerLayout->addWidget(m_vmTitleLabel);

    m_vmListWidget = new QWidget();
    m_vmListWidget->setStyleSheet(
        "QWidget {"
        "   background-color: #f5f7fa;"
        "   border-radius: 8px;"
        "}"
    );

    QVBoxLayout *vmListLayout = new QVBoxLayout(m_vmListWidget);
    vmListLayout->setSpacing(15);
    vmListLayout->setContentsMargins(20, 20, 20, 20);

    containerLayout->addWidget(m_vmListWidget);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    QString buttonStyle = (
        "QPushButton {"
        "   background-color: white;"
        "   border: 2px solid #e1e8ed;"
        "   border-radius: 8px;"
        "   font-size: 14px;"
        "   padding: 10px 20px;"
        "   color: #4400ff;"
        "}"
        "QPushButton:hover {"
        "   border-color: #3b82f6;"
        "   background-color: #f0f9ff;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #e1e8ed;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #cbd5e1;"
        "   color: #94a3b8;"
        "}"
    );

    m_backButton = new QPushButton(translate("back"));
    m_backButton->setStyleSheet(buttonStyle);
    connect(m_backButton, &QPushButton::clicked, this, &LoginWindow::onBackClicked);
    buttonLayout->addWidget(m_backButton);

    buttonLayout->addStretch();

    m_refreshButton = new QPushButton(translate("refresh"));
    m_refreshButton->setStyleSheet(buttonStyle);
    connect(m_refreshButton, &QPushButton::clicked, this, &LoginWindow::onRefreshClicked);
    buttonLayout->addWidget(m_refreshButton);

    containerLayout->addLayout(buttonLayout);

    m_vmStatusLabel = new QLabel();
    m_vmStatusLabel->setAlignment(Qt::AlignCenter);
    m_vmStatusLabel->setStyleSheet(
        "font-size: 13px;"
        "color: #64748b;"
        "padding: 5px;"
    );
    m_vmStatusLabel->hide();
    containerLayout->addWidget(m_vmStatusLabel);

    mainLayout->addStretch();
    mainLayout->addWidget(containerWidget, 0, Qt::AlignCenter);
    mainLayout->addStretch();
}

// 创建虚拟机列表项控件
// 参数：vmName - 虚拟机名称, vmId - 虚拟机ID, status - 虚拟机状态
// 返回值：QFrame* - 创建的虚拟机列表项控件
// 功能：创建包含虚拟机名称、状态显示和操作按钮的控件
QFrame* LoginWindow::createVmItemWidget(const QString &vmName, const QString &vmId, const QString &status)
{
    QFrame *frame = new QFrame();
    frame->setStyleSheet(
        "QFrame {"
        "   background-color: #f5f7fa;"
        "   border: 2px solid #e1e8ed;"
        "   border-radius: 8px;"
        "   padding: 20px;"
        "}"
    );

    QVBoxLayout *frameLayout = new QVBoxLayout(frame);
    frameLayout->setSpacing(15);
    frameLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(15);

    QLabel *nameLabel = new QLabel(vmName);
    nameLabel->setStyleSheet(
        "font-size: 20px;"
        "font-weight: bold;"
        "color: #1e40af;"
        "background-color: #dbeafe;"
        "padding: 8px 12px;"
        "border-radius: 6px;"
    );
    headerLayout->addWidget(nameLabel);

    headerLayout->addStretch();

    QString statusText = status.isEmpty() ? translate("status_unknown") : status;
    QString statusColor = "#64748b";
    if (status == "running") {
        statusText = translate("status_running");
        statusColor = "#10b981";
    } else if (status == "stopped") {
        statusText = translate("status_stopped");
        statusColor = "#ef4444";
    } else if (status == "paused") {
        statusText = translate("status_paused");
        statusColor = "#f59e0b";
    }

    QLabel *statusLabel = new QLabel(statusText);
    statusLabel->setStyleSheet(
        "font-size: 14px;"
        "font-weight: bold;"
        "color: " + statusColor + ";"
        "padding: 8px 15px;"
        "background-color: white;"
        "border-radius: 6px;"
        "border: 2px solid " + statusColor + ";"
    );
    headerLayout->addWidget(statusLabel);

    m_vmStatusLabels[vmId] = statusLabel;

    frameLayout->addLayout(headerLayout);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    QString actionButtonStyle = (
        "QPushButton {"
        "   background-color: #93c5fd;"
        "   border: none;"
        "   border-radius: 6px;"
        "   font-size: 15px;"
        "   padding: 12px 24px;"
        "   color: white;"
        "}"
        "QPushButton:hover {"
        "   background-color: #60a5fa;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #1e40af;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #cbd5e1;"
        "   color: #94a3b8;"
        "}"
    );

    QPushButton *startButton = new QPushButton(translate("start"));
    startButton->setStyleSheet(actionButtonStyle);
    connect(startButton, &QPushButton::clicked, this, [this, vmId]() {
        onVmStartClicked(vmId);
    });
    buttonLayout->addWidget(startButton);

    m_vmStartButtons[vmId] = startButton;

    QPushButton *stopButton = new QPushButton(translate("stop"));
    stopButton->setStyleSheet(actionButtonStyle);
    connect(stopButton, &QPushButton::clicked, this, [this, vmId]() {
        onVmStopClicked(vmId);
    });
    buttonLayout->addWidget(stopButton);

    QPushButton *restartButton = new QPushButton(translate("restart"));
    restartButton->setStyleSheet(actionButtonStyle);
    connect(restartButton, &QPushButton::clicked, this, [this, vmId]() {
        onVmRestartClicked(vmId);
    });
    buttonLayout->addWidget(restartButton);

    QPushButton *restoreButton = new QPushButton(translate("restore"));
    restoreButton->setStyleSheet(actionButtonStyle);
    restoreButton->setVisible(false);
    m_vmRestoreButtons[vmId] = restoreButton;
    connect(restoreButton, &QPushButton::clicked, this, [this, vmId]() {
        onVmRestoreClicked(vmId);
    });
    buttonLayout->addWidget(restoreButton);

    QPushButton *connectButton = new QPushButton(translate("connect_vm"));
    connectButton->setStyleSheet(actionButtonStyle);
    connect(connectButton, &QPushButton::clicked, this, [this, vmId]() {
        onVmConnectClicked(vmId);
    });
    buttonLayout->addWidget(connectButton);
    
    m_vmConnectButtons[vmId] = connectButton;

    frameLayout->addLayout(buttonLayout);

    return frame;
}

// 清空虚拟机列表控件中的所有子项
// 功能：移除并删除虚拟机列表中的所有控件，为刷新列表做准备
void LoginWindow::updateVmListWidget()
{
    QVBoxLayout *vmListLayout = qobject_cast<QVBoxLayout*>(m_vmListWidget->layout());
    if (vmListLayout) {
        while (vmListLayout->count()) {
            QLayoutItem *item = vmListLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }
}

// 处理登录按钮点击事件
// 功能：验证输入的服务器地址、用户名和密码，然后发起服务器健康检查
void LoginWindow::onLoginClicked()
{
    QString server = m_serverEdit->text().trimmed();
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text().trimmed();

    if (server.isEmpty()) {
        QMessageBox::warning(this, translate("alert_title"), translate("enter_server_address"));
        m_serverEdit->setFocus();
        return;
    }

    if (username.isEmpty()) {
        QMessageBox::warning(this, translate("alert_title"), translate("enter_username"));
        m_usernameEdit->setFocus();
        return;
    }

    if (password.isEmpty()) {
        QMessageBox::warning(this, translate("alert_title"), translate("enter_password"));
        m_passwordEdit->setFocus();
        return;
    }

    m_loginButton->setEnabled(false);
    m_loginButton->setText(translate("logging_in"));
    m_statusLabel->clear();
    m_statusLabel->show();
    checkServerHealth(server, username, password);
}

// 检查服务器健康状态
// 参数：server - 服务器地址, username - 用户名, password - 密码
// 功能：向服务器发送健康检查请求，验证服务器是否可用
void LoginWindow::checkServerHealth(const QString &server, const QString &username, const QString &password)
{
    m_server = server;

    QString urlStr = buildApiUrl("api/v1/auth/health");

    QUrl url(urlStr);
    QNetworkRequest request(url);
    
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    m_healthCheckData.clear();
    
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, server, username, password]() {
        onHealthCheckReply(reply, server, username, password);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        QByteArray data = reply->readAll();
        m_healthCheckData.append(data);
    });
}

// 处理服务器健康检查响应
// 参数：reply - 网络响应对象, server - 服务器地址, username - 用户名, password - 密码
// 功能：根据健康检查结果决定是否继续登录流程
void LoginWindow::onHealthCheckReply(QNetworkReply *reply, const QString &server, const QString &username, const QString &password)
{
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        m_loginButton->setEnabled(true);
        m_loginButton->setText(translate("login"));
        m_statusLabel->setText(translate("network_error"));
        return;
    }

    QByteArray responseData = m_healthCheckData;
    reply->deleteLater();
    
    QString response = QString::fromUtf8(responseData).trimmed();

    if (response == "ok") {
        sendLoginRequest(server, username, password);
    } else {
        m_loginButton->setEnabled(true);
        m_loginButton->setText(translate("login"));
        m_statusLabel->setText(translate("login_failed"));
        QMessageBox::warning(this, translate("login_failed"), translate("server_unreachable"));
    }
}

// 发送登录请求到服务器
// 参数：server - 服务器地址, username - 用户名, password - 密码
// 功能：构建登录请求的JSON数据并发送到服务器
void LoginWindow::sendLoginRequest(const QString &server, const QString &username, const QString &password)
{
    QString urlStr = buildApiUrl("api/v1/auth/login");

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    QJsonObject json;
    json["username"] = username;
    json["password"] = password;
    json["client_type"] = "win_client";
    json["login_server"] = server;

    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    QNetworkReply *reply = m_networkManager->post(request, data);
    connect(reply, &QNetworkReply::errorOccurred, this, &LoginWindow::onLoginError);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLoginReply(reply);
    });
}

// 处理登录响应
// 参数：reply - 网络响应对象
// 功能：解析登录响应，成功则切换到虚拟机列表页面，失败则显示错误信息
void LoginWindow::onLoginReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_loginButton->setEnabled(true);
        m_loginButton->setText(translate("login"));
        m_statusLabel->setText(translate("invalid_username_password"));
        m_statusLabel->show();
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        m_loginButton->setEnabled(true);
        m_loginButton->setText(translate("login"));
        m_statusLabel->setText(translate("invalid_username_password"));
        m_statusLabel->show();
        return;
    }

    QJsonObject json = doc.object();

    if (json.contains("code") && json["code"].toInt() == 0) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        
        QJsonObject data = json["data"].toObject();
        m_token = data.value("token").toString();
        m_username = data.value("username").toString();
        m_server = m_serverEdit->text().trimmed();
        
        // 保存登录信息
        saveSettings();
        
        m_stackedWidget->setCurrentIndex(1);
        fetchVmList();

    } else {
        m_loginButton->setEnabled(true);
        m_loginButton->setText(translate("login"));
        m_statusLabel->setText(translate("invalid_username_password"));
        m_statusLabel->show();
        QMessageBox::warning(this, translate("login_failed"), translate("invalid_username_password"));
    }
}

// 处理登录错误
// 参数：error - 网络错误类型
// 功能：记录登录错误信息
void LoginWindow::onLoginError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error);
}

// 处理自动登录复选框状态改变事件
// 参数：state - 复选框状态
// 功能：自动登录状态改变时立即保存设置
void LoginWindow::onAutoLoginChanged(Qt::CheckState state)
{
    Q_UNUSED(state);
    // 立即保存设置，确保取消自动登录后不会在下次启动时自动登录
    saveSettings();
}

// 处理语言按钮点击事件
// 功能：显示语言选择菜单
void LoginWindow::onLanguageButtonClicked()
{
    m_languageButton->showMenu();
}

// 处理语言选择事件
// 参数：action - 被选中的语言菜单项
// 功能：根据用户选择更新应用语言
void LoginWindow::onLanguageSelected(QAction *action)
{
    QString languageCode = action->data().toString();
    updateLanguage(languageCode);
}

// 处理返回按钮点击事件
// 功能：从虚拟机列表页面返回到登录页面，并清除登录状态
void LoginWindow::onBackClicked()
{
    m_stackedWidget->setCurrentIndex(0);
    m_token.clear();
    m_username.clear();
    m_server.clear();
    
    // 恢复登录按钮状态
    m_loginButton->setEnabled(true);
    m_loginButton->setText(translate("login"));
    
    // 清空并隐藏状态标签
    m_statusLabel->clear();
    m_statusLabel->hide();
    
    // 只有在"记住密码"未被勾选时才清空密码
    if (!m_rememberPasswordCheckBox->isChecked()) {
        m_passwordEdit->clear();
    }
}

// 处理刷新按钮点击事件
// 功能：重新获取虚拟机列表
void LoginWindow::onRefreshClicked()
{
    fetchVmList();
}

// 获取虚拟机列表
// 功能：向服务器发送请求获取当前用户的虚拟机列表
void LoginWindow::fetchVmList()
{
    if (m_token.isEmpty()) {
        return;
    }

    QString urlStr = buildApiUrl("api/v1/users/" + m_username + "/vms");

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    m_vmListData.clear();

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        QByteArray data = reply->readAll();
        m_vmListData.append(data);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onVmListReply(reply);
    });
}

// 处理虚拟机列表响应
// 参数：reply - 网络响应对象
// 功能：解析虚拟机列表数据，创建虚拟机列表项并获取每个虚拟机的状态
void LoginWindow::onVmListReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        m_vmStatusLabel->setText(translate("network_error"));
        m_vmStatusLabel->show();
        return;
    }

    QByteArray responseData = m_vmListData;
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        m_vmStatusLabel->setText(translate("server_error"));
        m_vmStatusLabel->show();
        return;
    }

    QJsonObject json = doc.object();

    if (!json.contains("code") || json["code"].toInt() != 0) {
        m_vmStatusLabel->setText(translate("server_error"));
        m_vmStatusLabel->show();
        return;
    }

    QJsonArray vmArray = json["data"].toArray();

    updateVmListWidget();

    QVBoxLayout *vmListLayout = qobject_cast<QVBoxLayout*>(m_vmListWidget->layout());
    if (!vmListLayout) {
        return;
    }

    m_vmStatusLabels.clear();
    m_vmStartButtons.clear();

    for (const QJsonValue &vmValue : vmArray) {
        QJsonObject vmObj = vmValue.toObject();
        QString vmId = QString::number(vmObj.value("vmid").toInt());
        QString vmName = vmObj.value("vm_name").toString();

        QFrame *vmFrame = createVmItemWidget(vmName, vmId, "");
        vmListLayout->addWidget(vmFrame);

        fetchVmStatus(vmId);
    }
}

// 处理虚拟机启动按钮点击事件
// 参数：vmId - 虚拟机ID
// 功能：向服务器发送启动虚拟机的请求
void LoginWindow::onVmStartClicked(const QString &vmId)
{
    if (m_token.isEmpty()) {
        return;
    }

    QPushButton *startButton = m_vmStartButtons.value(vmId);
    if (startButton) {
        startButton->setEnabled(false);
    }

    QString urlStr = buildApiUrl("api/v1/vm/" + vmId + "/start");

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    m_vmStartData[vmId].clear();

    QNetworkReply *reply = m_networkManager->post(request, QByteArray());
    
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(5000);
    connect(timer, &QTimer::timeout, this, [this, reply, vmId, timer]() {
        if (reply && reply->isRunning()) {
            reply->abort();
        }
        QPushButton *startButton = m_vmStartButtons.value(vmId);
        if (startButton) {
            startButton->setEnabled(true);
        }
        timer->deleteLater();
    });

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, vmId]() {
        QByteArray data = reply->readAll();
        m_vmStartData[vmId].append(data);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId, timer]() {
        timer->stop();
        timer->deleteLater();
        onVmStartReply(reply, vmId);
    });

    timer->start();
}

// 处理虚拟机启动响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析启动响应，成功则更新虚拟机状态为运行中
void LoginWindow::onVmStartReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QPushButton *startButton = m_vmStartButtons.value(vmId);
        if (startButton) {
            startButton->setEnabled(true);
        }
        return;
    }

    QByteArray responseData = m_vmStartData[vmId];
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        QPushButton *startButton = m_vmStartButtons.value(vmId);
        if (startButton) {
            startButton->setEnabled(true);
        }
        return;
    }

    QJsonObject json = doc.object();

    if (json.contains("code") && json["code"].toInt() == 0) {
        updateVmStatus(vmId, "running");
    } else {
        QPushButton *startButton = m_vmStartButtons.value(vmId);
        if (startButton) {
            startButton->setEnabled(true);
        }
    }
}

// 处理虚拟机停止按钮点击事件
// 参数：vmId - 虚拟机ID
// 功能：发送关机请求，成功则更新虚拟机状态为关机，失败则不更新状态
void LoginWindow::onVmStopClicked(const QString &vmId)
{
    QString url = buildApiUrl(QString("/api/v1/vm/%1/shutdown").arg(vmId));
    
    QNetworkRequest request(url);
    request.setRawHeader(QByteArray("Authorization"), QByteArray("Bearer " + m_token.toUtf8()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    
    QNetworkReply *reply = m_networkManager->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId]() {
        onVmStopReply(reply, vmId);
    });
}

// 处理虚拟机关机响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析关机响应，成功则更新虚拟机状态为关机，失败则不更新状态
void LoginWindow::onVmStopReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "onVmStopReply: error =" << reply->error() << reply->errorString();
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "onVmStopReply: responseData =" << responseData;

    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "onVmStopReply: invalid JSON response";
        return;
    }

    QJsonObject json = doc.object();
    qDebug() << "onVmStopReply: json =" << json;

    if (json.contains("code") && json["code"].toInt() == 0) {
        updateVmStatus(vmId, "stopped");
    }
}

// 处理虚拟机重启按钮点击事件
// 参数：vmId - 虚拟机ID
// 功能：发送重启请求，成功或失败都不更新虚拟机状态
void LoginWindow::onVmRestartClicked(const QString &vmId)
{
    QString url = buildApiUrl(QString("/api/v1/vm/%1/restart").arg(vmId));
    
    QNetworkRequest request(url);
    request.setRawHeader(QByteArray("Authorization"), QByteArray("Bearer " + m_token.toUtf8()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    
    QNetworkReply *reply = m_networkManager->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId]() {
        onQVmRestartReply(reply, vmId);
    });
}

// 处理虚拟机重启响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析重启响应，成功或失败都不更新虚拟机状态
void LoginWindow::onQVmRestartReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "onQVmRestartReply: error =" << reply->error() << reply->errorString();
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "onQVmRestartReply: responseData =" << responseData;

    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "onQVmRestartReply: invalid JSON response";
        return;
    }

    QJsonObject json = doc.object();
    qDebug() << "onQVmRestartReply: json =" << json;

    if (json.contains("code") && json["code"].toInt() == 0) {
        qDebug() << "onQVmRestartReply: restart success";
    } else {
        qDebug() << "onQVmRestartReply: restart failed";
    }
}

// 处理虚拟机恢复按钮点击事件
// 参数：vmId - 虚拟机ID
// 功能：发送还原请求，成功则更新虚拟机状态为关机，失败则不更新状态
void LoginWindow::onVmRestoreClicked(const QString &vmId)
{
    QString url = buildApiUrl(QString("/api/v1/vm/%1/rollback").arg(vmId));
    
    QNetworkRequest request(url);
    request.setRawHeader(QByteArray("Authorization"), QByteArray("Bearer " + m_token.toUtf8()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    
    QNetworkReply *reply = m_networkManager->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId]() {
        onVmRestoreReply(reply, vmId);
    });
}

// 处理虚拟机还原响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析还原响应，成功则更新虚拟机状态为关机，失败则不更新状态
void LoginWindow::onVmRestoreReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "onVmRestoreReply: error =" << reply->error() << reply->errorString();
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "onVmRestoreReply: responseData =" << responseData;

    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "onVmRestoreReply: invalid JSON response";
        return;
    }

    QJsonObject json = doc.object();
    qDebug() << "onVmRestoreReply: json =" << json;

    if (json.contains("code") && json["code"].toInt() == 0) {
        updateVmStatus(vmId, "stopped");
    }
}

// 处理虚拟机连接按钮点击事件
// 参数：vmId - 虚拟机ID
// 功能：发送请求获取RDP文件，下载成功后获取连接命令
void LoginWindow::onVmConnectClicked(const QString &vmId)
{
    qDebug() << "onVmConnectClicked: vmId =" << vmId;
    
    QString url = buildApiUrl(QString("/api/v1/vm/%1/rdp").arg(vmId));
    qDebug() << "onVmConnectClicked: url =" << url;
    
    QNetworkRequest request(url);
    request.setRawHeader(QByteArray("Authorization"), QByteArray("Bearer " + m_token.toUtf8()));
    
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId]() {
        onVmRdpFileReply(reply, vmId);
    });
}

// 处理RDP文件下载响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析RDP文件下载响应，成功则保存文件并获取连接命令，失败则提示连接失败
void LoginWindow::onVmRdpFileReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    qDebug() << "onVmRdpFileReply: vmId =" << vmId;

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "onVmRdpFileReply: error =" << reply->error() << reply->errorString();
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }

    QByteArray rdpData = reply->readAll();
    qDebug() << "onVmRdpFileReply: rdpData.size() =" << rdpData.size();
    
    // 保存RDP文件到用户应用数据目录
    QString appLocalAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    qDebug() << "onVmRdpFileReply: appLocalAppData =" << appLocalAppData;
    
    QDir appDataDir(appLocalAppData);
    if (!appDataDir.exists()) {
        qDebug() << "onVmRdpFileReply: creating app data directory";
        appDataDir.mkpath(".");
    }
    
    QString rdpFilePath = appDataDir.filePath("template.rdp");
    qDebug() << "onVmRdpFileReply: rdpFilePath =" << rdpFilePath;
    
    QFile rdpFile(rdpFilePath);
    if (!rdpFile.open(QIODevice::WriteOnly)) {
        qDebug() << "onVmRdpFileReply: failed to open file for writing";
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }
    
    qint64 bytesWritten = rdpFile.write(rdpData);
    if (bytesWritten != rdpData.size()) {
        qDebug() << "onVmRdpFileReply: failed to write all data, written =" << bytesWritten << "expected =" << rdpData.size();
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }
    
    if (!rdpFile.flush()) {
        qDebug() << "onVmRdpFileReply: failed to flush file";
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }
    
    rdpFile.close();
    
    if (!QFile::exists(rdpFilePath)) {
        qDebug() << "onVmRdpFileReply: file does not exist after writing";
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }
    
    QFileInfo fileInfo(rdpFilePath);
    if (fileInfo.size() != rdpData.size()) {
        qDebug() << "onVmRdpFileReply: file size mismatch, actual =" << fileInfo.size() << "expected =" << rdpData.size();
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }
    
    qDebug() << "onVmRdpFileReply: file saved successfully";
    
    // 获取连接命令
    QString url = buildApiUrl(QString("/api/v1/vm/%1/login").arg(vmId));
    qDebug() << "onVmRdpFileReply: login url =" << url;
    
    QNetworkRequest request(url);
    request.setRawHeader(QByteArray("Authorization"), QByteArray("Bearer " + m_token.toUtf8()));
    request.setRawHeader(QByteArray("Content-Type"), QByteArray("application/json"));
    
    QNetworkReply *loginReply = m_networkManager->get(request);
    connect(loginReply, &QNetworkReply::finished, this, [this, loginReply, vmId]() {
        onVmLoginReply(loginReply, vmId);
    });
}

// 处理获取连接命令响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析连接命令响应，成功则执行连接命令，失败则提示连接失败
void LoginWindow::onVmLoginReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    qDebug() << "onVmLoginReply: vmId =" << vmId;

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "onVmLoginReply: error =" << reply->error() << reply->errorString();
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "onVmLoginReply: responseData =" << responseData;

    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "onVmLoginReply: invalid JSON response";
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
        return;
    }

    QJsonObject json = doc.object();
    qDebug() << "onVmLoginReply: json =" << json;

    if (json.contains("code") && json["code"].toInt() == 0) {
        QJsonObject data = json["data"].toObject();
        QString command = data.value("command").toString();
        
        qDebug() << "onVmLoginReply: command =" << command;
        
        if (command.isEmpty()) {
            qDebug() << "onVmLoginReply: command is empty";
            QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
            return;
        }
        
        // 执行连接命令
        QString appDir = QCoreApplication::applicationDirPath();
        qDebug() << "onVmLoginReply: appDir =" << appDir;
        
        QDir dir(appDir);
        QString binDir;
        
        // 尝试在应用程序目录下查找bin目录
        if (dir.exists("bin")) {
            binDir = dir.absoluteFilePath("bin");
        } else {
            // 向上查找bin目录
            while (dir.cdUp()) {
                if (dir.exists("bin")) {
                    binDir = dir.absoluteFilePath("bin");
                    break;
                }
            }
        }
        
        // 如果还是找不到，就使用应用程序目录/bin
        if (binDir.isEmpty()) {
            binDir = appDir + "/bin";
        }
        
        // 使用用户应用数据目录中的RDP文件
        QString appLocalAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QString rdpFilePath = QDir(appLocalAppData).filePath("template.rdp");
        
        qDebug() << "onVmLoginReply: binDir =" << binDir;
        qDebug() << "onVmLoginReply: rdpFilePath =" << rdpFilePath;
        
        QStringList args = command.split(" ", Qt::SkipEmptyParts);
        if (args.isEmpty()) {
            qDebug() << "onVmLoginReply: command is empty";
            QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
            return;
        }
        
        QString program = args.takeFirst();
        QString programPath = binDir + "/" + program;
        
        // 替换RDP文件路径为用户目录中的完整路径
        for (int i = 0; i < args.size(); ++i) {
            if (args[i].contains("template.rdp")) {
                args[i] = rdpFilePath;
            }
        }
        
        qDebug() << "onVmLoginReply: program =" << program;
        qDebug() << "onVmLoginReply: programPath =" << programPath;
        qDebug() << "onVmLoginReply: arguments =" << args;
        
        if (m_rdpProcess) {
            if (m_rdpProcess->state() == QProcess::Running) {
                m_rdpProcess->terminate();
                if (!m_rdpProcess->waitForFinished(3000)) {
                    m_rdpProcess->kill();
                }
            }
            delete m_rdpProcess;
            m_rdpProcess = nullptr;
        }
        
        m_rdpProcess = new QProcess(this);
        m_rdpProcess->setWorkingDirectory(binDir);
        
        connect(m_rdpProcess, &QProcess::readyReadStandardOutput, this, &LoginWindow::onRdpProcessReadyReadStandardOutput);
        connect(m_rdpProcess, &QProcess::readyReadStandardError, this, &LoginWindow::onRdpProcessReadyReadStandardError);
        connect(m_rdpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &LoginWindow::onRdpProcessFinished);
        connect(m_rdpProcess, &QProcess::errorOccurred, this, &LoginWindow::onRdpProcessErrorOccurred);
        
        m_rdpProcess->start(programPath, args);
        
        qDebug() << "onVmLoginReply: starting process";
        qDebug() << "onVmLoginReply: workingDirectory =" << m_rdpProcess->workingDirectory();
        
        if (m_rdpProcess->waitForStarted()) {
            qDebug() << "onVmLoginReply: process started successfully";
            m_disconnectButton->show();
        } else {
            qDebug() << "onVmLoginReply: failed to start process";
            qDebug() << "onVmLoginReply: error =" << m_rdpProcess->error();
            qDebug() << "onVmLoginReply: errorString =" << m_rdpProcess->errorString();
        }
    } else {
        qDebug() << "onVmLoginReply: code =" << json["code"].toInt();
        QMessageBox::warning(this, translate("connect"), translate("connect_failed"));
    }
}

void LoginWindow::onRdpProcessReadyReadStandardOutput()
{
    if (!m_rdpProcess) {
        return;
    }
    QByteArray data = m_rdpProcess->readAllStandardOutput();
    qDebug() << "onRdpProcessReadyReadStandardOutput:" << data;
}

void LoginWindow::onRdpProcessReadyReadStandardError()
{
    if (!m_rdpProcess) {
        return;
    }
    QByteArray data = m_rdpProcess->readAllStandardError();
    qDebug() << "onRdpProcessReadyReadStandardError:" << data;
    
    if (data.contains("ERRINFO_LOGOFF_BY_USER")) {
        qDebug() << "onRdpProcessReadyReadStandardError: Detected ERRINFO_LOGOFF_BY_USER, terminating process";
        if (m_rdpProcess->state() == QProcess::Running) {
            m_rdpProcess->terminate();
            if (!m_rdpProcess->waitForFinished(1000)) {
                m_rdpProcess->kill();
            }
        }
    }
}

void LoginWindow::onRdpProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "onRdpProcessFinished: exitCode =" << exitCode;
    qDebug() << "onRdpProcessFinished: exitStatus =" << exitStatus;
    
    if (m_rdpProcess) {
        qDebug() << "onRdpProcessFinished: readAllStandardOutput =" << m_rdpProcess->readAllStandardOutput();
        qDebug() << "onRdpProcessFinished: readAllStandardError =" << m_rdpProcess->readAllStandardError();
    }
    
    m_disconnectButton->hide();
}

void LoginWindow::onRdpProcessErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "onRdpProcessErrorOccurred: error =" << error;
    if (m_rdpProcess) {
        qDebug() << "onRdpProcessErrorOccurred: errorString =" << m_rdpProcess->errorString();
    }
}

bool LoginWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        qDebug() << "eventFilter: Key event detected, watched:" << watched << "type:" << event->type();
        qDebug() << "m_rdpProcess:" << m_rdpProcess;
        if (m_rdpProcess) {
            qDebug() << "m_rdpProcess state:" << m_rdpProcess->state() << "Running:" << QProcess::Running;
        }
        
        if (m_rdpProcess && m_rdpProcess->state() == QProcess::Running) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            qDebug() << "Global key event intercepted, key:" << keyEvent->key() << "type:" << event->type();
            
            if (event->type() == QEvent::KeyPress) {
                QByteArray keyData;
                
                if (keyEvent->modifiers() & Qt::ControlModifier) {
                    keyData.append("\x1D");
                }
                if (keyEvent->modifiers() & Qt::AltModifier) {
                    keyData.append("\x38");
                }
                if (keyEvent->modifiers() & Qt::ShiftModifier) {
                    keyData.append("\x2A");
                }
                
                switch (keyEvent->key()) {
                    case Qt::Key_Return:
                    case Qt::Key_Enter:
                        keyData.append("\x1C\x0D");
                        break;
                    case Qt::Key_Escape:
                        keyData.append("\x01");
                        break;
                    case Qt::Key_Tab:
                        keyData.append("\x0F");
                        break;
                    case Qt::Key_Backspace:
                        keyData.append("\x0E");
                        break;
                    case Qt::Key_Delete:
                        keyData.append("\x53");
                        break;
                    case Qt::Key_Up:
                        keyData.append("\x48");
                        break;
                    case Qt::Key_Down:
                        keyData.append("\x50");
                        break;
                    case Qt::Key_Left:
                        keyData.append("\x4B");
                        break;
                    case Qt::Key_Right:
                        keyData.append("\x4D");
                        break;
                    case Qt::Key_F1:
                        keyData.append("\x3B");
                        break;
                    case Qt::Key_F2:
                        keyData.append("\x3C");
                        break;
                    case Qt::Key_F3:
                        keyData.append("\x3D");
                        break;
                    case Qt::Key_F4:
                        keyData.append("\x3E");
                        break;
                    case Qt::Key_F5:
                        keyData.append("\x3F");
                        break;
                    case Qt::Key_F6:
                        keyData.append("\x40");
                        break;
                    case Qt::Key_F7:
                        keyData.append("\x41");
                        break;
                    case Qt::Key_F8:
                        keyData.append("\x42");
                        break;
                    case Qt::Key_F9:
                        keyData.append("\x43");
                        break;
                    case Qt::Key_F10:
                        keyData.append("\x44");
                        break;
                    case Qt::Key_F11:
                        keyData.append("\x57");
                        break;
                    case Qt::Key_F12:
                        keyData.append("\x58");
                        break;
                    default:
                        if (!keyEvent->text().isEmpty()) {
                            QString text = keyEvent->text();
                            for (int i = 0; i < text.length(); ++i) {
                                QChar c = text[i];
                                if (c.isLetterOrNumber()) {
                                    keyData.append(c.toLatin1());
                                }
                            }
                        }
                        break;
                }
                
                if (!keyData.isEmpty()) {
                    qDebug() << "Sending key data to RDP:" << keyData.toHex();
                    m_rdpProcess->write(keyData);
                }
            }
            
            return true;
        }
    }
    
    if (watched == m_disconnectButton) {
        if (event->type() == QEvent::Enter) {
            qDebug() << "eventFilter: Enter";
            if (m_disconnectButtonDocked) {
                qDebug() << "Button is docked, popping out";
                m_disconnectButton->move(m_disconnectButtonDockedPosition);
                m_disconnectButtonDocked = false;
            }
        } else if (event->type() == QEvent::Leave) {
            qDebug() << "eventFilter: Leave";
            QScreen *screen = QGuiApplication::primaryScreen();
            QRect screenGeometry = screen->geometry();
            QPoint currentPos = m_disconnectButton->pos();
            
            if (currentPos.x() < 0) {
                int visibleWidth = 6 + (m_randomGenerator.bounded(0, 5));
                m_disconnectButton->move(-m_disconnectButton->width() + visibleWidth, currentPos.y());
                m_disconnectButtonDocked = true;
                qDebug() << "Button left screen, auto-dredocking to left edge";
            } else if (currentPos.x() > screenGeometry.width() - m_disconnectButton->width()) {
                int visibleWidth = 6 + (m_randomGenerator.bounded(0, 5));
                m_disconnectButton->move(screenGeometry.width() - visibleWidth, currentPos.y());
                m_disconnectButtonDocked = true;
                qDebug() << "Button right screen, auto-dredocking to right edge";
            } else if (currentPos.y() < 0) {
                int visibleHeight = 6 + (m_randomGenerator.bounded(0, 5));
                m_disconnectButton->move(currentPos.x(), -m_disconnectButton->height() + visibleHeight);
                m_disconnectButtonDocked = true;
                qDebug() << "Button above screen, auto-dredocking to top edge";
            } else if (currentPos.y() > screenGeometry.height() - m_disconnectButton->height()) {
                int visibleHeight = 6 + (m_randomGenerator.bounded(0, 5));
                m_disconnectButton->move(currentPos.x(), screenGeometry.height() - visibleHeight);
                m_disconnectButtonDocked = true;
                qDebug() << "Button below screen, auto-dredocking to bottom edge";
            } else {
                m_disconnectButtonDockedPosition = currentPos;
                qDebug() << "Button is fully visible, updating docked position:" << m_disconnectButtonDockedPosition;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            qDebug() << "eventFilter: MouseButtonPress";
            qDebug() << "button:" << mouseEvent->button();
            if (mouseEvent->button() == Qt::LeftButton) {
                qDebug() << "Left button pressed in eventFilter";
                m_disconnectButtonDragging = true;
                m_disconnectButtonActuallyDragged = false;
                m_disconnectButtonPressPosition = mouseEvent->globalPosition().toPoint();
                m_disconnectButtonDragPosition = mouseEvent->globalPosition().toPoint() - m_disconnectButton->pos();
                qDebug() << "m_disconnectButtonPressPosition:" << m_disconnectButtonPressPosition;
                qDebug() << "m_disconnectButtonDragPosition:" << m_disconnectButtonDragPosition;
            }
        } else if (event->type() == QEvent::MouseMove) {
            qDebug() << "eventFilter: MouseMove, m_disconnectButtonDragging:" << m_disconnectButtonDragging;
            if (m_disconnectButtonDragging) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                QPoint newPos = mouseEvent->globalPosition().toPoint() - m_disconnectButtonDragPosition;
                qDebug() << "Moving button to:" << newPos;
                
                QPoint currentPos = mouseEvent->globalPosition().toPoint();
                int distance = (currentPos - m_disconnectButtonPressPosition).manhattanLength();
                qDebug() << "Drag distance:" << distance;
                if (distance > 5) {
                    m_disconnectButtonActuallyDragged = true;
                    qDebug() << "Actually dragged!";
                }
                
                QScreen *screen = QGuiApplication::primaryScreen();
                QRect screenGeometry = screen->geometry();
                qDebug() << "Screen geometry:" << screenGeometry;
                qDebug() << "Button position:" << newPos;
                qDebug() << "Button size:" << m_disconnectButton->size();
                qDebug() << "Bottom threshold:" << (screenGeometry.height() - m_disconnectButton->height() - 10);
                
                bool docked = false;
                
                if (newPos.x() < 10) {
                    int visibleWidth = 6 + (m_randomGenerator.bounded(0, 5));
                    m_disconnectButtonDockedPosition = newPos;
                    m_disconnectButtonDockedPosition.setX(0);
                    newPos.setX(-m_disconnectButton->width() + visibleWidth);
                    m_disconnectButtonDocked = true;
                    docked = true;
                    qDebug() << "Auto-docking to left edge, visible width:" << visibleWidth;
                } else if (newPos.x() > screenGeometry.width() - m_disconnectButton->width() - 10) {
                    int visibleWidth = 6 + (m_randomGenerator.bounded(0, 5));
                    m_disconnectButtonDockedPosition = newPos;
                    m_disconnectButtonDockedPosition.setX(screenGeometry.width() - m_disconnectButton->width());
                    newPos.setX(screenGeometry.width() - visibleWidth);
                    m_disconnectButtonDocked = true;
                    docked = true;
                    qDebug() << "Auto-docking to right edge, visible width:" << visibleWidth;
                }
                
                if (newPos.y() < 10) {
                    int visibleHeight = 6 + (m_randomGenerator.bounded(0, 5));
                    m_disconnectButtonDockedPosition = newPos;
                    m_disconnectButtonDockedPosition.setY(0);
                    newPos.setY(-m_disconnectButton->height() + visibleHeight);
                    m_disconnectButtonDocked = true;
                    docked = true;
                    qDebug() << "Auto-docking to top edge, visible height:" << visibleHeight;
                } else if (newPos.y() > screenGeometry.height() - m_disconnectButton->height() - 10) {
                    int visibleHeight = 6 + (m_randomGenerator.bounded(0, 5));
                    m_disconnectButtonDockedPosition = newPos;
                    m_disconnectButtonDockedPosition.setY(screenGeometry.height() - m_disconnectButton->height());
                    newPos.setY(screenGeometry.height() - visibleHeight);
                    m_disconnectButtonDocked = true;
                    docked = true;
                    qDebug() << "Auto-docking to bottom edge, visible height:" << visibleHeight;
                }
                
                if (!docked) {
                    m_disconnectButtonDocked = false;
                }
                
                m_disconnectButton->move(newPos);
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            qDebug() << "eventFilter: MouseButtonRelease";
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            qDebug() << "button:" << mouseEvent->button();
            if (mouseEvent->button() == Qt::LeftButton) {
                qDebug() << "Stopping drag in eventFilter";
                qDebug() << "m_disconnectButtonActuallyDragged:" << m_disconnectButtonActuallyDragged;
                if (m_disconnectButtonActuallyDragged) {
                    qDebug() << "Blocking click event because button was dragged";
                    m_disconnectButtonDragging = false;
                    return true;
                }
                m_disconnectButtonDragging = false;
                qDebug() << "Executing disconnect because button was clicked";
                if (m_rdpProcess && m_rdpProcess->state() == QProcess::Running) {
                    qDebug() << "Terminating RDP process";
                    m_rdpProcess->terminate();
                    if (!m_rdpProcess->waitForFinished(1000)) {
                        qDebug() << "Killing RDP process";
                        m_rdpProcess->kill();
                    }
                }
                m_disconnectButton->hide();
                qDebug() << "Disconnect button hidden";
                return true;
            }
        }
    }
    
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        qDebug() << "eventFilter: Key event detected, watched:" << watched << "type:" << event->type();
        qDebug() << "m_rdpProcess:" << m_rdpProcess;
        if (m_rdpProcess) {
            qDebug() << "m_rdpProcess state:" << m_rdpProcess->state() << "Running:" << QProcess::Running;
        }
        
        if (m_rdpProcess && m_rdpProcess->state() == QProcess::Running) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            qDebug() << "Global key event intercepted, key:" << keyEvent->key() << "type:" << event->type();
            
            if (event->type() == QEvent::KeyPress) {
                QByteArray keyData;
                
                if (keyEvent->modifiers() & Qt::ControlModifier) {
                    keyData.append("\x1D");
                }
                if (keyEvent->modifiers() & Qt::AltModifier) {
                    keyData.append("\x38");
                }
                if (keyEvent->modifiers() & Qt::ShiftModifier) {
                    keyData.append("\x2A");
                }
                
                switch (keyEvent->key()) {
                    case Qt::Key_Return:
                    case Qt::Key_Enter:
                        keyData.append("\x1C\x0D");
                        break;
                    case Qt::Key_Escape:
                        keyData.append("\x01");
                        break;
                    case Qt::Key_Tab:
                        keyData.append("\x0F");
                        break;
                    case Qt::Key_Backspace:
                        keyData.append("\x0E");
                        break;
                    case Qt::Key_Delete:
                        keyData.append("\x53");
                        break;
                    case Qt::Key_Up:
                        keyData.append("\x48");
                        break;
                    case Qt::Key_Down:
                        keyData.append("\x50");
                        break;
                    case Qt::Key_Left:
                        keyData.append("\x4B");
                        break;
                    case Qt::Key_Right:
                        keyData.append("\x4D");
                        break;
                    case Qt::Key_F1:
                        keyData.append("\x3B");
                        break;
                    case Qt::Key_F2:
                        keyData.append("\x3C");
                        break;
                    case Qt::Key_F3:
                        keyData.append("\x3D");
                        break;
                    case Qt::Key_F4:
                        keyData.append("\x3E");
                        break;
                    case Qt::Key_F5:
                        keyData.append("\x3F");
                        break;
                    case Qt::Key_F6:
                        keyData.append("\x40");
                        break;
                    case Qt::Key_F7:
                        keyData.append("\x41");
                        break;
                    case Qt::Key_F8:
                        keyData.append("\x42");
                        break;
                    case Qt::Key_F9:
                        keyData.append("\x43");
                        break;
                    case Qt::Key_F10:
                        keyData.append("\x44");
                        break;
                    case Qt::Key_F11:
                        keyData.append("\x57");
                        break;
                    case Qt::Key_F12:
                        keyData.append("\x58");
                        break;
                    default:
                        if (!keyEvent->text().isEmpty()) {
                            QString text = keyEvent->text();
                            for (int i = 0; i < text.length(); ++i) {
                                QChar c = text[i];
                                if (c.isLetterOrNumber()) {
                                    keyData.append(c.toLatin1());
                                }
                            }
                        }
                        break;
                }
                
                if (!keyData.isEmpty()) {
                    qDebug() << "Sending key data to RDP:" << keyData.toHex();
                    m_rdpProcess->write(keyData);
                }
            }
            
            return true;
        }
    }
    
    return QMainWindow::eventFilter(watched, event);
}

void LoginWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}

void LoginWindow::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "mousePressEvent called";
    qDebug() << "m_disconnectButton:" << m_disconnectButton;
    if (m_disconnectButton) {
        qDebug() << "m_disconnectButton->isVisible():" << m_disconnectButton->isVisible();
    }
    if (m_disconnectButton && m_disconnectButton->isVisible()) {
        if (event->button() == Qt::LeftButton) {
            qDebug() << "Left button pressed";
            QPoint buttonPos = m_disconnectButton->mapFromGlobal(event->globalPosition().toPoint());
            QRect buttonRect = m_disconnectButton->rect();
            qDebug() << "buttonPos:" << buttonPos << "buttonRect:" << buttonRect;
            qDebug() << "buttonRect.contains(buttonPos):" << buttonRect.contains(buttonPos);
            if (buttonRect.contains(buttonPos)) {
                qDebug() << "Starting drag";
                m_disconnectButtonDragging = true;
                m_disconnectButtonDragPosition = event->globalPosition().toPoint() - m_disconnectButton->pos();
                qDebug() << "m_disconnectButtonDragPosition:" << m_disconnectButtonDragPosition;
            }
        }
    }
    QMainWindow::mousePressEvent(event);
}

void LoginWindow::mouseMoveEvent(QMouseEvent *event)
{
    qDebug() << "mouseMoveEvent called, m_disconnectButtonDragging:" << m_disconnectButtonDragging;
    if (m_disconnectButtonDragging && m_disconnectButton) {
        QPoint newPos = event->globalPosition().toPoint() - m_disconnectButtonDragPosition;
        qDebug() << "Moving button to:" << newPos;
        m_disconnectButton->move(newPos);
    }
    QMainWindow::mouseMoveEvent(event);
}

void LoginWindow::mouseReleaseEvent(QMouseEvent *event)
{
    qDebug() << "mouseReleaseEvent called, button:" << event->button();
    if (event->button() == Qt::LeftButton) {
        qDebug() << "Stopping drag";
        m_disconnectButtonDragging = false;
    }
    QMainWindow::mouseReleaseEvent(event);
}

void LoginWindow::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "keyPressEvent called, key:" << event->key() << "modifiers:" << event->modifiers();
    qDebug() << "Key text:" << event->text();
    
    if (m_rdpProcess && m_rdpProcess->state() == QProcess::Running) {
        QByteArray keyData;
        
        if (event->modifiers() & Qt::ControlModifier) {
            keyData.append("\x1D");
        }
        if (event->modifiers() &  Qt::AltModifier) {
            keyData.append("\x38");
        }
        if (event->modifiers() & Qt::ShiftModifier) {
            keyData.append("\x2A");
        }
        
        switch (event->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                keyData.append("\x1C\x0D");
                break;
            case Qt::Key_Escape:
                keyData.append("\x01");
                break;
            case Qt::Key_Tab:
                keyData.append("\x0F");
                break;
            case Qt::Key_Backspace:
                keyData.append("\x0E");
                break;
            case Qt::Key_Delete:
                keyData.append("\x53");
                break;
            case Qt::Key_Up:
                keyData.append("\x48");
                break;
            case Qt::Key_Down:
                keyData.append("\x50");
                break;
            case Qt::Key_Left:
                keyData.append("\x4B");
                break;
            case Qt::Key_Right:
                keyData.append("\x4D");
                break;
            case Qt::Key_F1:
                keyData.append("\x3B");
                break;
            case Qt::Key_F2:
                keyData.append("\x3C");
                break;
            case Qt::Key_F3:
                keyData.append("\x3D");
                break;
            case Qt::Key_F4:
                keyData.append("\x3E");
                break;
            case Qt::Key_F5:
                keyData.append("\x3F");
                break;
            case Qt::Key_F6:
                keyData.append("\x40");
                break;
            case Qt::Key_F7:
                keyData.append("\x41");
                break;
            case Qt::Key_F8:
                keyData.append("\x42");
                break;
            case Qt::Key_F9:
                keyData.append("\x43");
                break;
            case Qt::Key_F10:
                keyData.append("\x44");
                break;
            case Qt::Key_F11:
                keyData.append("\x57");
                break;
            case Qt::Key_F12:
                keyData.append("\x58");
                break;
            default:
                if (!event->text().isEmpty()) {
                    QString text = event->text();
                    for (int i = 0; i < text.length(); ++i) {
                        QChar c = text[i];
                        if (c.isLetterOrNumber()) {
                            keyData.append(c.toLatin1());
                        }
                    }
                }
                break;
        }
        
        if (!keyData.isEmpty()) {
            qDebug() << "Sending key data to RDP:" << keyData.toHex();
            m_rdpProcess->write(keyData);
        }
        
        event->accept();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void LoginWindow::keyReleaseEvent(QKeyEvent *event)
{
    qDebug() << "keyReleaseEvent called, key:" << event->key() << "modifiers:" << event->modifiers();
    if (m_rdpProcess && m_rdpProcess->state() == QProcess::Running) {
        event->accept();
    } else {
        QMainWindow::keyReleaseEvent(event);
    }
}

// 获取虚拟机状态
// 参数：vmId - 虚拟机ID
// 功能：向服务器发送请求获取指定虚拟机的当前状态
void LoginWindow::fetchVmStatus(const QString &vmId)
{
    if (m_token.isEmpty()) {
        return;
    }

    QString urlStr = buildApiUrl("api/v1/vm/" + vmId + "/currentstatus");

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    m_vmStatusData[vmId].clear();

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, vmId]() {
        QByteArray data = reply->readAll();
        m_vmStatusData[vmId].append(data);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId]() {
        onVmStatusReply(reply, vmId);
    });
}

// 处理虚拟机状态响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析虚拟机状态数据并更新UI显示
void LoginWindow::onVmStatusReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        return;
    }

    QByteArray responseData = m_vmStatusData[vmId];
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        return;
    }

    QJsonObject json = doc.object();

    if (!json.contains("code") || json["code"].toInt() != 0) {
        return;
    }

    QJsonObject data = json["data"].toObject();
    QString status = data.value("status").toString();

    updateVmStatus(vmId, status);
}

// 更新虚拟机状态显示
// 参数：vmId - 虚拟机ID, status - 虚拟机状态
// 功能：根据虚拟机状态更新UI显示，包括状态文本和颜色
void LoginWindow::updateVmStatus(const QString &vmId, const QString &status)
{
    QLabel *statusLabel = m_vmStatusLabels.value(vmId);
    if (!statusLabel) {
        return;
    }

    QString statusText;
    QString statusColor = "#64748b";
    
    if (status == "running") {
        statusText = translate("status_running");
        statusColor = "#10b981";
    } else if (status == "stopped") {
        statusText = translate("status_stopped");
        statusColor = "#ef4444";
    } else if (status == "paused") {
        statusText = translate("status_paused");
        statusColor = "#f59e0b";
    } else {
        statusText = translate("status_unknown");
        statusColor = "#dc3545";
    }

    statusLabel->setText(statusText);
    statusLabel->setStyleSheet(
        "font-size: 13px;"
        "color: " + statusColor + ";"
        "padding: 5px 10px;"
        "background-color: white;"
        "border-radius: 6px;"
        "border: 1px solid " + statusColor + ";"
    );
    
    fetchVmSnapshot(vmId);
}

// 查询虚拟机快照
// 参数：vmId - 虚拟机ID
// 功能：向服务器发送请求查询虚拟机是否有快照
void LoginWindow::fetchVmSnapshot(const QString &vmId)
{
    if (m_token.isEmpty()) {
        return;
    }

    QString urlStr = buildApiUrl("api/v1/vm/" + vmId + "/hasmilestone");

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, vmId]() {
        onVmSnapshotReply(reply, vmId);
    });
}

// 处理虚拟机快照响应
// 参数：reply - 网络响应对象, vmId - 虚拟机ID
// 功能：解析快照响应，有快照则显示还原按钮，无快照或失败则隐藏还原按钮
void LoginWindow::onVmSnapshotReply(QNetworkReply *reply, const QString &vmId)
{
    reply->deleteLater();

    if (isTokenExpired(reply)) {
        handleTokenExpired();
        return;
    }

    QPushButton *restoreButton = m_vmRestoreButtons.value(vmId);
    if (!restoreButton) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "onVmSnapshotReply: error =" << reply->error() << reply->errorString();
        restoreButton->setVisible(false);
        m_vmHasSnapshot[vmId] = false;
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "onVmSnapshotReply: responseData =" << responseData;

    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "onVmSnapshotReply: invalid JSON response";
        restoreButton->setVisible(false);
        m_vmHasSnapshot[vmId] = false;
        return;
    }

    QJsonObject json = doc.object();
    qDebug() << "onVmSnapshotReply: json =" << json;

    if (json.contains("code") && json["code"].toInt() == 0) {
        restoreButton->setVisible(true);
        m_vmHasSnapshot[vmId] = true;
    } else {
        restoreButton->setVisible(false);
        m_vmHasSnapshot[vmId] = false;
    }
}

// 初始化多语言翻译字典
// 功能：为所有支持的语言添加翻译文本
void LoginWindow::initTranslations()
{
    m_translations["en_US"]["title"] = "VDI Login";
    m_translations["en_US"]["server"] = "Server";
    m_translations["en_US"]["username"] = "Username";
    m_translations["en_US"]["password"] = "Password";
    m_translations["en_US"]["remember_password"] = "Remember Password";
    m_translations["en_US"]["auto_login"] = "Auto Login";
    m_translations["en_US"]["login"] = "Login";
    m_translations["en_US"]["logging_in"] = "Logging in...";
    m_translations["en_US"]["network_error"] = "Network error, please check server address";
    m_translations["en_US"]["alert_title"] = "Alert";
    m_translations["en_US"]["enter_server_address"] = "Please enter server address";
    m_translations["en_US"]["enter_username"] = "Please enter username";
    m_translations["en_US"]["enter_password"] = "Please enter password";
    m_translations["en_US"]["success"] = "Success";
    m_translations["en_US"]["login_success"] = "Login successful!";
    m_translations["en_US"]["login_failed"] = "Login failed";
    m_translations["en_US"]["server_unreachable"] = "Server unreachable";
    m_translations["en_US"]["invalid_username_password"] = "Invalid username or password";
    m_translations["en_US"]["token_expired"] = "Token expired, please login again";
    m_translations["en_US"]["vm_list_title"] = "Virtual Machine List";
    m_translations["en_US"]["back"] = "Back";
    m_translations["en_US"]["refresh"] = "Refresh";
    m_translations["en_US"]["server_error"] = "Server error";
    m_translations["en_US"]["start"] = "Start";
    m_translations["en_US"]["stop"] = "Shutdown";
    m_translations["en_US"]["restart"] = "Restart";
    m_translations["en_US"]["restore"] = "Restore";
    m_translations["en_US"]["connect"] = "Connect";
    m_translations["en_US"]["start_vm"] = "Start virtual machine";
    m_translations["en_US"]["stop_vm"] = "Stop virtual machine";
    m_translations["en_US"]["restart_vm"] = "Restart virtual machine";
    m_translations["en_US"]["restore_vm"] = "Restore virtual machine";
    m_translations["en_US"]["connect_vm"] = "Connect";
    m_translations["en_US"]["connect_failed"] = "Connection failed";
    m_translations["en_US"]["connect_success"] = "Connection successful";
    m_translations["en_US"]["disconnect"] = "Disconnect";
    m_translations["en_US"]["status_running"] = "Running";
    m_translations["en_US"]["status_stopped"] = "Stopped";
    m_translations["en_US"]["status_paused"] = "Paused";
    m_translations["en_US"]["status_unknown"] = "Unknown";

    m_translations["zh_CN"]["title"] = "VDI 登录";
    m_translations["zh_CN"]["server"] = "服务器";
    m_translations["zh_CN"]["username"] = "用户名";
    m_translations["zh_CN"]["password"] = "密码";
    m_translations["zh_CN"]["remember_password"] = "记住密码";
    m_translations["zh_CN"]["auto_login"] = "自动登录";
    m_translations["zh_CN"]["login"] = "登录";
    m_translations["zh_CN"]["logging_in"] = "登录中...";
    m_translations["zh_CN"]["network_error"] = "网络错误，请检查服务器地址";
    m_translations["zh_CN"]["alert_title"] = "提示";
    m_translations["zh_CN"]["enter_server_address"] = "请输入服务器地址";
    m_translations["zh_CN"]["enter_username"] = "请输入用户名";
    m_translations["zh_CN"]["enter_password"] = "请输入密码";
    m_translations["zh_CN"]["success"] = "成功";
    m_translations["zh_CN"]["login_success"] = "登录成功！";
    m_translations["zh_CN"]["login_failed"] = "登录失败";
    m_translations["zh_CN"]["server_unreachable"] = "服务器不可达";
    m_translations["zh_CN"]["invalid_username_password"] = "输入的用户名或密码有误，请重新输入";
    m_translations["zh_CN"]["token_expired"] = "Token已过期，请重新登录";
    m_translations["zh_CN"]["vm_list_title"] = "虚拟机列表";
    m_translations["zh_CN"]["back"] = "返回";
    m_translations["zh_CN"]["refresh"] = "刷新";
    m_translations["zh_CN"]["server_error"] = "服务器错误";
    m_translations["zh_CN"]["start"] = "开机";
    m_translations["zh_CN"]["stop"] = "关机";
    m_translations["zh_CN"]["restart"] = "重启";
    m_translations["zh_CN"]["restore"] = "还原";
    m_translations["zh_CN"]["start_vm"] = "开机";
    m_translations["zh_CN"]["stop_vm"] = "关机";
    m_translations["zh_CN"]["restart_vm"] = "重启";
    m_translations["zh_CN"]["restore_vm"] = "还原";
    m_translations["zh_CN"]["connect"] = "连接";
    m_translations["zh_CN"]["connect_vm"] = "连接";
    m_translations["zh_CN"]["connect_failed"] = "连接失败";
    m_translations["zh_CN"]["connect_success"] = "连接成功";
    m_translations["zh_CN"]["disconnect"] = "结束连接";
    m_translations["zh_CN"]["status_running"] = "运行中";
    m_translations["zh_CN"]["status_stopped"] = "已关机";
    m_translations["zh_CN"]["status_paused"] = "已暂停";
    m_translations["zh_CN"]["status_unknown"] = "未知";

    m_translations["zh_TW"]["title"] = "VDI 登入";
    m_translations["zh_TW"]["server"] = "伺服器";
    m_translations["zh_TW"]["username"] = "用戶名";
    m_translations["zh_TW"]["password"] = "密碼";
    m_translations["zh_TW"]["remember_password"] = "記住密碼";
    m_translations["zh_TW"]["auto_login"] = "自動登入";
    m_translations["zh_TW"]["login"] = "登入";
    m_translations["zh_TW"]["logging_in"] = "登入中...";
    m_translations["zh_TW"]["network_error"] = "網路錯誤，請檢查伺服器地址";
    m_translations["zh_TW"]["alert_title"] = "提示";
    m_translations["zh_TW"]["enter_server_address"] = "請輸入伺服器地址";
    m_translations["zh_TW"]["enter_username"] = "請輸入用戶名";
    m_translations["zh_TW"]["enter_password"] = "請輸入密碼";
    m_translations["zh_TW"]["success"] = "成功";
    m_translations["zh_TW"]["login_success"] = "登入成功！";
    m_translations["zh_TW"]["login_failed"] = "登入失敗";
    m_translations["zh_TW"]["server_unreachable"] = "伺服器不可達";
    m_translations["zh_TW"]["invalid_username_password"] = "輸入的用戶名或密碼有誤，請重新輸入";
    m_translations["zh_TW"]["vm_list_title"] = "虛擬機列表";
    m_translations["zh_TW"]["back"] = "返回";
    m_translations["zh_TW"]["refresh"] = "刷新";
    m_translations["zh_TW"]["server_error"] = "伺服器錯誤";
    m_translations["zh_TW"]["start"] = "開機";
    m_translations["zh_TW"]["stop"] = "關機";
    m_translations["zh_TW"]["restart"] = "重啟";
    m_translations["zh_TW"]["restore"] = "還原";
    m_translations["zh_TW"]["start_vm"] = "開機虛擬機";
    m_translations["zh_TW"]["stop_vm"] = "關機虛擬機";
    m_translations["zh_TW"]["restart_vm"] = "重啟虛擬機";
    m_translations["zh_TW"]["restore_vm"] = "還原虛擬機";
    m_translations["zh_TW"]["connect"] = "連接";
    m_translations["zh_TW"]["connect_vm"] = "連接";
    m_translations["zh_TW"]["connect_failed"] = "連接失敗";
    m_translations["zh_TW"]["connect_success"] = "連接成功";
    m_translations["zh_TW"]["disconnect"] = "結束連接";
    m_translations["zh_TW"]["status_running"] = "運行中";
    m_translations["zh_TW"]["status_stopped"] = "已關機";
    m_translations["zh_TW"]["status_paused"] = "已暫停";
    m_translations["zh_TW"]["status_unknown"] = "未知";

    m_translations["ja_JP"]["title"] = "VDI ログイン";
    m_translations["ja_JP"]["server"] = "サーバー";
    m_translations["ja_JP"]["username"] = "ユーザー名";
    m_translations["ja_JP"]["password"] = "パスワード";
    m_translations["ja_JP"]["remember_password"] = "パスワードを保存";
    m_translations["ja_JP"]["auto_login"] = "自動ログイン";
    m_translations["ja_JP"]["login"] = "ログイン";
    m_translations["ja_JP"]["logging_in"] = "ログイン中...";
    m_translations["ja_JP"]["network_error"] = "ネットワークエラー、サーバーアドレスを確認してください";
    m_translations["ja_JP"]["alert_title"] = "警告";
    m_translations["ja_JP"]["enter_server_address"] = "サーバーアドレスを入力してください";
    m_translations["ja_JP"]["enter_username"] = "ユーザー名を入力してください";
    m_translations["ja_JP"]["enter_password"] = "パスワードを入力してください";
    m_translations["ja_JP"]["success"] = "成功";
    m_translations["ja_JP"]["login_success"] = "ログイン成功！";
    m_translations["ja_JP"]["login_failed"] = "ログイン失敗";
    m_translations["ja_JP"]["server_unreachable"] = "サーバーに接続できません";
    m_translations["ja_JP"]["invalid_username_password"] = "ユーザー名またはパスワードが間違っています";
    m_translations["ja_JP"]["vm_list_title"] = "仮想マシン一覧";
    m_translations["ja_JP"]["back"] = "戻る";
    m_translations["ja_JP"]["refresh"] = "更新";
    m_translations["ja_JP"]["server_error"] = "サーバーエラー";
    m_translations["ja_JP"]["start"] = "起動";
    m_translations["ja_JP"]["stop"] = "停止";
    m_translations["ja_JP"]["restart"] = "再起動";
    m_translations["ja_JP"]["restore"] = "復元";
    m_translations["ja_JP"]["connect"] = "接続";
    m_translations["ja_JP"]["start_vm"] = "仮想マシンを起動";
    m_translations["ja_JP"]["stop_vm"] = "仮想マシンを停止";
    m_translations["ja_JP"]["restart_vm"] = "仮想マシンを再起動";
    m_translations["ja_JP"]["restore_vm"] = "仮想マシンを復元";
    m_translations["ja_JP"]["connect_vm"] = "接続";
    m_translations["ja_JP"]["connect_failed"] = "接続失敗";
    m_translations["ja_JP"]["connect_success"] = "接続成功";
    m_translations["ja_JP"]["disconnect"] = "接続を終了";
    m_translations["ja_JP"]["status_running"] = "実行中";
    m_translations["ja_JP"]["status_stopped"] = "停止中";
    m_translations["ja_JP"]["status_paused"] = "一時停止";
    m_translations["ja_JP"]["status_unknown"] = "不明";
}

// 更新应用语言
// 参数：languageCode - 语言代码（如：en_US, zh_CN, zh_TW, ja_JP）
// 功能：更新所有UI控件的文本为指定语言
void LoginWindow::updateLanguage(const QString &languageCode)
{
    m_currentLanguage = languageCode;

    m_titleLabel->setText(translate("title"));
    m_serverEdit->setPlaceholderText(translate("server"));
    m_usernameEdit->setPlaceholderText(translate("username"));
    m_passwordEdit->setPlaceholderText(translate("password"));
    m_rememberPasswordCheckBox->setText(translate("remember_password"));
    m_autoLoginCheckBox->setText(translate("auto_login"));
    m_loginButton->setText(translate("login"));
    m_vmTitleLabel->setText(translate("vm_list_title"));
    m_backButton->setText(translate("back"));
    m_refreshButton->setText(translate("refresh"));
    
    // 更新断开连接按钮的文本
    if (m_disconnectButton) {
        m_disconnectButton->setText(translate("disconnect"));
    }
    
    // 更新所有虚拟机连接按钮的文本
    for (auto it = m_vmConnectButtons.begin(); it != m_vmConnectButtons.end(); ++it) {
        if (it.value()) {
            it.value()->setText(translate("connect"));
        }
    }
    
    // 保存语言设置
    saveSettings();
}

// 翻译文本
// 参数：key - 翻译键值
// 返回值：QString - 翻译后的文本
// 功能：根据当前语言和键值返回对应的翻译文本
QString LoginWindow::translate(const QString &key)
{
    if (m_translations.contains(m_currentLanguage) && m_translations[m_currentLanguage].contains(key)) {
        return m_translations[m_currentLanguage][key];
    }
    return key;
}

// 构建API URL
// 参数：path - API路径
// 返回值：QString - 完整的API URL
// 功能：根据服务器地址构建完整的API请求URL
QString LoginWindow::buildApiUrl(const QString &path)
{
    QString server = m_server.trimmed();
    
    if (!server.startsWith("https://") && !server.startsWith("http://")) {
        server = "https://" + server;
    }
    
    QUrl url(server);
    int port = url.port();
    
    if (port == -1) {
        port = 443;
    }
    
    QString host = url.host();
    if (host.isEmpty()) {
        host = server;
        if (host.startsWith("https://")) {
            host = host.mid(8);
        } else if (host.startsWith("http://")) {
            host = host.mid(7);
        }
    }
    
    QString result = "https://" + host + ":" + QString::number(port);
    if (!path.startsWith("/")) {
        result += "/";
    }
    result += path;
    return result;
}

// 加载保存的设置
// 功能：从配置文件中加载服务器地址、用户名、密码等设置
void LoginWindow::loadSettings()
{
    QSettings settings("VDIClient", "Login");
    
    QString server = settings.value("server", "").toString();
    QString username = settings.value("username", "").toString();
    QString password = settings.value("password", "").toString();
    bool rememberPassword = settings.value("rememberPassword", false).toBool();
    bool autoLogin = settings.value("autoLogin", false).toBool();
    QString language = settings.value("language", "en_US").toString();
    
    if (!server.isEmpty()) {
        m_serverEdit->setText(server);
    }
    if (!username.isEmpty()) {
        m_usernameEdit->setText(username);
    }
    if (!password.isEmpty() && rememberPassword) {
        m_passwordEdit->setText(password);
    }
    m_rememberPasswordCheckBox->setChecked(rememberPassword);
    m_autoLoginCheckBox->setChecked(autoLogin);
    
    if (!language.isEmpty()) {
        updateLanguage(language);
    }
}

// 保存设置
// 功能：将服务器地址、用户名、密码等设置保存到配置文件
void LoginWindow::saveSettings()
{
    QSettings settings("VDIClient", "Login");
    
    QString server = m_serverEdit->text().trimmed();
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();
    bool rememberPassword = m_rememberPasswordCheckBox->isChecked();
    bool autoLogin = m_autoLoginCheckBox->isChecked();
    
    settings.setValue("server", server);
    settings.setValue("username", username);
    
    if (rememberPassword) {
        settings.setValue("password", password);
    } else {
        settings.remove("password");
    }
    
    settings.setValue("rememberPassword", rememberPassword);
    settings.setValue("autoLogin", autoLogin);
    settings.setValue("language", m_currentLanguage);
}

bool LoginWindow::isTokenExpired(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return statusCode == 401;
}

void LoginWindow::handleTokenExpired()
{
    qDebug() << "Token expired, returning to login page";
    
    m_token.clear();
    
    if (m_disconnectButton) {
        m_disconnectButton->hide();
    }
    
    if (m_rdpProcess && m_rdpProcess->state() == QProcess::Running) {
        m_rdpProcess->terminate();
        if (!m_rdpProcess->waitForFinished(3000)) {
            m_rdpProcess->kill();
        }
    }
    
    m_stackedWidget->setCurrentIndex(0);
    
    if (m_statusLabel) {
        m_statusLabel->setText(translate("token_expired"));
        m_statusLabel->show();
    }
    
    if (m_loginButton) {
        m_loginButton->setEnabled(true);
        m_loginButton->setText(translate("login"));
    }
}
