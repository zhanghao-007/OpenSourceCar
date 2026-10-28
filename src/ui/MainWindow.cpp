#include "MainWindow.h"
#include "ClientsWidget.h"
#include "MessagesWidget.h"
#include "CommandWidget.h"
#include "TelemetryWidget.h"
#include "LogPanelWidget.h"
#include "HistoryWidget.h"
#include "../core/CarRegistry.h"
#include "../core/DeviceProtocol.h"

#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QSettings>
#include <QFont>
#include <QCoreApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // ---- 业务对象（必须先于 buildUi，遥测看板需要 registry）----
    m_registry = new CarRegistry(this);

    // ---- 本地数据库（默认与 exe 同目录，可被 --db 覆盖）----
    m_db = new DatabaseManager(this);
    connect(m_db, &DatabaseManager::errorOccurred,
            this, &MainWindow::onDbError, Qt::QueuedConnection);
    {
        const QString defaultDb = QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/carbroker.db");
        if (m_db->open(defaultDb))
            log(QStringLiteral("已启用本地数据库：%1").arg(defaultDb), 0);
        else
            log(QStringLiteral("本地数据库未启用：%1").arg(m_db->lastError()), 1);
    }

    buildUi();

    m_broker       = new Mqtt::MqttBroker(nullptr);   // 不能有 parent，稍后 moveToThread
    m_brokerThread = new QThread(this);
    m_broker->moveToThread(m_brokerThread);
    connect(m_brokerThread, &QThread::finished, m_broker, &QObject::deleteLater);
    m_brokerThread->start();

    connectBrokerSignals();

    m_uiTimer = new QTimer(this);
    connect(m_uiTimer, &QTimer::timeout, this, [this]() {
        m_telemetryWidget->refreshTable();
        m_commandWidget->setCarIds(m_registry->carIds());
        updateStatusBar();
    });
    m_uiTimer->start(1000);

    onLoadSettings();
    log(QStringLiteral("小车通信后台已就绪，点击「启动服务」开始监听"), 0);
}

MainWindow::~MainWindow()
{
    if (m_brokerThread && m_brokerThread->isRunning()) {
        if (m_broker)
            QMetaObject::invokeMethod(m_broker, "stop", Qt::QueuedConnection);
        m_brokerThread->quit();
        m_brokerThread->wait(3000);
    }
}

// ---------------------------------------------------------------------------
// 界面搭建
// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("小车通信后台 · MQTT 服务端  v1.0"));

    buildMenuBar();

    QWidget *central = new QWidget(this);
    QVBoxLayout *main = new QVBoxLayout(central);
    main->setContentsMargins(8, 8, 8, 8);
    main->setSpacing(8);

    // ---- 顶部服务控制条 ----
    QHBoxLayout *bar = new QHBoxLayout();
    bar->addWidget(new QLabel(QStringLiteral("监听地址："), this));

    m_editAddress = new QLineEdit(this);
    m_editAddress->setText(QStringLiteral("0.0.0.0"));
    m_editAddress->setToolTip(QStringLiteral("0.0.0.0 表示监听所有网卡"));
    m_editAddress->setMaximumWidth(120);
    bar->addWidget(m_editAddress);

    bar->addWidget(new QLabel(QStringLiteral("端口："), this));
    m_spinPort = new QSpinBox(this);
    m_spinPort->setRange(1, 65535);
    m_spinPort->setValue(1883);
    m_spinPort->setMaximumWidth(90);
    bar->addWidget(m_spinPort);

    m_btnStart = new QPushButton(QStringLiteral("启动服务"), this);
    m_btnStart->setMinimumWidth(100);
    bar->addWidget(m_btnStart);

    m_labState = new QLabel(QStringLiteral("● 未运行"), this);
    m_labState->setStyleSheet(QStringLiteral("color:#999999;font-weight:bold;"));
    bar->addWidget(m_labState);

    bar->addSpacing(16);
    m_chkAuth = new QCheckBox(QStringLiteral("启用用户名密码"), this);
    bar->addWidget(m_chkAuth);

    m_editUser = new QLineEdit(this);
    m_editUser->setPlaceholderText(QStringLiteral("用户名"));
    m_editUser->setMaximumWidth(100);
    bar->addWidget(m_editUser);

    m_editPass = new QLineEdit(this);
    m_editPass->setPlaceholderText(QStringLiteral("密码"));
    m_editPass->setEchoMode(QLineEdit::Password);
    m_editPass->setMaximumWidth(100);
    bar->addWidget(m_editPass);

    bar->addSpacing(16);
    bar->addWidget(new QLabel(QStringLiteral("最大连接："), this));
    m_spinMaxClients = new QSpinBox(this);
    m_spinMaxClients->setRange(1, 10000);
    m_spinMaxClients->setValue(500);
    m_spinMaxClients->setMaximumWidth(90);
    bar->addWidget(m_spinMaxClients);

    bar->addStretch();
    main->addLayout(bar);

    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);

    // ---- 标签页 ----
    m_tabs = new QTabWidget(this);
    m_clientsWidget   = new ClientsWidget(this);
    m_messagesWidget  = new MessagesWidget(this);
    m_commandWidget   = new CommandWidget(this);
    m_telemetryWidget = new TelemetryWidget(m_registry, this);

    m_tabs->addTab(m_clientsWidget,   QStringLiteral("连接管理"));
    m_tabs->addTab(m_messagesWidget,  QStringLiteral("消息监控"));
    m_tabs->addTab(m_commandWidget,   QStringLiteral("指令下发"));
    m_tabs->addTab(m_telemetryWidget, QStringLiteral("遥测看板"));

    m_logPanel = new LogPanelWidget(this);
    m_tabs->addTab(m_logPanel, QStringLiteral("运行日志"));

    m_historyWidget = new HistoryWidget(m_db, this);
    m_tabs->addTab(m_historyWidget, QStringLiteral("历史数据库"));

    main->addWidget(m_tabs, 1);
    setCentralWidget(central);

    buildStatusBar();

    connect(m_clientsWidget, &ClientsWidget::kickRequested,
            this, &MainWindow::onKickClient);
    connect(m_commandWidget, &CommandWidget::publishRequested,
            this, &MainWindow::onPublishCommand);
}

void MainWindow::buildMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction *saveAct = new QAction(QStringLiteral("保存配置"), this);
    QAction *loadAct = new QAction(QStringLiteral("读取配置"), this);
    QAction *exitAct = new QAction(QStringLiteral("退出"), this);
    fileMenu->addAction(saveAct);
    fileMenu->addAction(loadAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSaveSettings);
    connect(loadAct, &QAction::triggered, this, &MainWindow::onLoadSettings);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    QAction *aboutAct = new QAction(QStringLiteral("关于"), this);
    helpMenu->addAction(aboutAct);
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("关于"),
            QStringLiteral("小车通信后台 · MQTT 服务端\n\n"
                           "基于 Qt 5.12 自研的 MQTT 3.1.1 Broker，支持：\n"
                           "  · QoS 0/1/2 完整消息流程\n"
                           "  · 保留消息、遗嘱消息、持久会话\n"
                           "  · 主题通配符 + 与 # 匹配\n"
                           "  · 连接管理 / 消息监控 / 指令下发 / 遥测看板\n"
                           "  · 本地 SQLite 数据库（遥测与消息持久化、可回查导出）\n\n"
                           "小车遥测主题：car/<编号>/telemetry\n"
                           "指令下发主题：car/<编号>/cmd"));
    });
}

void MainWindow::buildStatusBar()
{
    m_labListen   = new QLabel(QStringLiteral("未监听"), this);
    m_labClients  = new QLabel(QStringLiteral("连接 0"), this);
    m_labMessages = new QLabel(QStringLiteral("上行 0 / 下行 0"), this);
    m_labDevice   = new QLabel(QStringLiteral("设备帧 0"), this);
    m_labUptime   = new QLabel(QStringLiteral("运行时长 00:00:00"), this);

    statusBar()->addWidget(m_labListen);
    statusBar()->addWidget(m_labClients, 1);
    statusBar()->addWidget(m_labMessages);
    statusBar()->addWidget(m_labDevice);
    statusBar()->addPermanentWidget(m_labUptime);
}

// ---------------------------------------------------------------------------
// Broker 信号接线
// ---------------------------------------------------------------------------
void MainWindow::connectBrokerSignals()
{
    connect(m_broker, &Mqtt::MqttBroker::brokerStarted,
            this, &MainWindow::onBrokerStarted, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::brokerStopped,
            this, &MainWindow::onBrokerStopped, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::brokerError,
            this, &MainWindow::onBrokerError, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::logLine,
            this, &MainWindow::onLogLine, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::clientConnected,
            this, &MainWindow::onClientConnected, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::clientDisconnected,
            this, &MainWindow::onClientDisconnected, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::clientsSnapshot,
            this, &MainWindow::onClientsSnapshot, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::messageRecord,
            this, &MainWindow::onMessageRecord, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::statsUpdated,
            this, &MainWindow::onStatsUpdated, Qt::QueuedConnection);
    connect(m_broker, &Mqtt::MqttBroker::packetSniffed,
            this, &MainWindow::onPacketSniffed, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------
bool MainWindow::setLogFile(const QString &path)
{
    if (!m_logPanel)
        return false;
    const bool ok = m_logPanel->setLogFile(path);
    if (ok && !path.isEmpty())
        log(QStringLiteral("运行日志已落盘到 %1").arg(path), 0);
    return ok;
}

void MainWindow::applyCommandLine(const QStringList &args)
{
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--port") && i + 1 < args.size()) {
            m_spinPort->setValue(args.at(i + 1).toInt());
        } else if (args.at(i) == QLatin1String("--address") && i + 1 < args.size()) {
            m_editAddress->setText(args.at(i + 1));
        } else if (args.at(i) == QLatin1String("--logfile") && i + 1 < args.size()) {
            setLogFile(args.at(i + 1));
        } else if (args.at(i) == QLatin1String("--db") && i + 1 < args.size()) {
            const QString dbPath = args.at(i + 1);
            if (m_db) {
                m_db->close();
                if (m_db->open(dbPath))
                    log(QStringLiteral("数据库已切换到：%1").arg(dbPath), 0);
                else
                    log(QStringLiteral("数据库切换失败：%1").arg(m_db->lastError()), 2);
            }
        } else if (args.at(i) == QLatin1String("--devtopic") && i + 1 < args.size()) {
            const QString prefix = args.at(i + 1).trimmed();
            if (!prefix.isEmpty()) {
                m_devTopicPrefix = prefix;
                log(QStringLiteral("设备链路上报主题前缀：%1").arg(prefix), 0);
            }
        }
    }
    if (args.contains(QLatin1String("--autostart"))) {
        // 延后一帧执行，确保事件循环已跑起来
        QTimer::singleShot(200, this, &MainWindow::onStartStopClicked);
    }
}

void MainWindow::onStartStopClicked()
{
    if (!m_running) {
        Mqtt::BrokerConfig cfg;
        cfg.port           = static_cast<quint16>(m_spinPort->value());
        cfg.bindAddress    = m_editAddress->text().trimmed();
        cfg.maxClients     = m_spinMaxClients->value();
        cfg.requireAuth    = m_chkAuth->isChecked();
        cfg.allowAnonymous = !m_chkAuth->isChecked();
        cfg.authUsername   = m_editUser->text().trimmed();
        cfg.authPassword   = m_editPass->text();

        // 跨线程：配置先入队，再入队启动命令，顺序有保证
        QMetaObject::invokeMethod(m_broker, "setConfig", Qt::QueuedConnection,
                                  Q_ARG(Mqtt::BrokerConfig, cfg));
        QMetaObject::invokeMethod(m_broker, "start", Qt::QueuedConnection,
                                  Q_ARG(quint16, cfg.port),
                                  Q_ARG(QString, cfg.bindAddress));
    } else {
        QMetaObject::invokeMethod(m_broker, "stop", Qt::QueuedConnection);
    }
}

void MainWindow::onBrokerStarted(quint16 port, const QString &address)
{
    m_running   = true;
    m_startedAt = QDateTime::currentDateTime();
    setRunningState(true);
    m_labListen->setText(QStringLiteral("监听 %1:%2").arg(address).arg(port));
}

void MainWindow::onBrokerStopped()
{
    m_running = false;
    setRunningState(false);
    m_labListen->setText(QStringLiteral("未监听"));
    m_clientsWidget->clearAll();
}

void MainWindow::onBrokerError(const QString &text)
{
    log(text, 2);
    QMessageBox::warning(this, QStringLiteral("服务端错误"), text);
}

void MainWindow::setRunningState(bool running)
{
    m_btnStart->setText(running ? QStringLiteral("停止服务") : QStringLiteral("启动服务"));
    m_labState->setText(running ? QStringLiteral("● 运行中") : QStringLiteral("● 未运行"));
    m_labState->setStyleSheet(running
                                  ? QStringLiteral("color:#2e7d32;font-weight:bold;")
                                  : QStringLiteral("color:#999999;font-weight:bold;"));
    m_editAddress->setEnabled(!running);
    m_spinPort->setEnabled(!running);
    m_spinMaxClients->setEnabled(!running);
}

// ---------------------------------------------------------------------------
// 事件转发
// ---------------------------------------------------------------------------
void MainWindow::onClientConnected(const Mqtt::ClientViewInfo &info)
{
    log(QStringLiteral("+ 连接 %1 (%2)").arg(info.clientId).arg(info.peerAddress), 0);
    if (m_db && m_db->isOpen())
        m_db->upsertCar(info.clientId, true, QStringLiteral("ONLINE"),
                        QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::onClientDisconnected(const QString &clientId, const QString &reason)
{
    log(QStringLiteral("- 断开 %1：%2").arg(clientId).arg(reason), 0);
    m_registry->markOffline(clientId);
    if (m_db && m_db->isOpen())
        m_db->upsertCar(clientId, false, QStringLiteral("OFFLINE"),
                        QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::onClientsSnapshot(const QList<Mqtt::ClientViewInfo> &list)
{
    m_clientsWidget->onClientsSnapshot(list);
}

void MainWindow::onMessageRecord(const Mqtt::MessageRecord &rec)
{
    m_messagesWidget->onMessage(rec);
    m_registry->onMessage(rec);

    // 运行日志里打印消息本身（方向过滤与每秒限流由日志面板负责）
    if (m_logPanel)
        m_logPanel->appendMessage(rec);

    // ---- 落库 ----
    if (m_db && m_db->isOpen()) {
        m_db->enqueueMessage(rec);
        // 遥测只在"上行 IN"时落库一次；被转发给订阅者的 OUT 副本不再重复存储
        if (rec.direction == QLatin1String("IN")) {
            QString carId, kind;
            if (m_registry->parseCarTopic(rec.topic, carId, kind)
                    && kind == QLatin1String("telemetry")) {
                m_db->enqueueTelemetry(m_registry->car(carId), rec.payload);
            }
        }
    }

    // ---- 设备链路报文（device/link/req）：解析成结构化字段后落库 ----
    // 只处理上行 IN，避免把转发给订阅者的 OUT 副本重复入库。
    if (rec.direction == QLatin1String("IN")
            && Dev::isDeviceTopic(rec.topic, m_devTopicPrefix)) {
        Dev::Frame frame;
        QString err;
        if (Dev::parse(rec, frame, &err)) {
            ++m_devOk;
            if (m_db && m_db->isOpen())
                m_db->enqueueDevice(frame);
        } else {
            ++m_devFail;
            if (m_logPanel)
                m_logPanel->appendLog(QStringLiteral("设备报文解析失败 [%1]：%2")
                                          .arg(rec.topic, err), 1);
        }
    }

    if (rec.direction == QLatin1String("IN"))
        m_msgIn++;
    else
        m_msgOut++;
}

void MainWindow::onPacketSniffed(const QString &clientId, const QString &direction,
                                 const QByteArray &raw)
{
    if (m_logPanel)
        m_logPanel->appendPacket(clientId, direction, raw);
}

void MainWindow::onStatsUpdated(const Mqtt::BrokerStats &stats)
{
    m_labClients->setText(QStringLiteral("连接 %1   订阅 %2   保留 %3   会话 %4")
                              .arg(stats.activeConnections)
                              .arg(stats.subscriptions)
                              .arg(stats.retainedCount)
                              .arg(stats.sessions));
}

void MainWindow::updateStatusBar()
{
    m_labMessages->setText(QStringLiteral("上行 %1 / 下行 %2").arg(m_msgIn).arg(m_msgOut));
    m_labDevice->setText(QStringLiteral("设备帧 %1%2")
                             .arg(m_devOk)
                             .arg(m_devFail > 0
                                      ? QStringLiteral("（失败 %1）").arg(m_devFail)
                                      : QString()));
    if (m_running && m_startedAt.isValid()) {
        const qint64 secs = m_startedAt.secsTo(QDateTime::currentDateTime());
        m_labUptime->setText(QStringLiteral("运行时长 %1:%2:%3")
                                 .arg(secs / 3600, 2, 10, QLatin1Char('0'))
                                 .arg((secs % 3600) / 60, 2, 10, QLatin1Char('0'))
                                 .arg(secs % 60, 2, 10, QLatin1Char('0')));
    } else {
        m_labUptime->setText(QStringLiteral("运行时长 00:00:00"));
    }
}

void MainWindow::onPublishCommand(const QString &topic, const QByteArray &payload,
                                  Mqtt::QoS qos, bool retain)
{
    if (!m_running) {
        QMessageBox::warning(this, QStringLiteral("无法下发"),
                             QStringLiteral("MQTT 服务端尚未启动"));
        return;
    }
    QMetaObject::invokeMethod(m_broker, "publishLocal", Qt::QueuedConnection,
                              Q_ARG(QString, topic),
                              Q_ARG(QByteArray, payload),
                              Q_ARG(Mqtt::QoS, qos),
                              Q_ARG(bool, retain));
}

void MainWindow::onKickClient(const QString &clientId)
{
    QMetaObject::invokeMethod(m_broker, "kickClient", Qt::QueuedConnection,
                              Q_ARG(QString, clientId));
}

// ---------------------------------------------------------------------------
// 配置存取
// ---------------------------------------------------------------------------
void MainWindow::onSaveSettings()
{
    QSettings s(QStringLiteral("OpenSorceCar.ini"), QSettings::IniFormat);
    s.setValue(QStringLiteral("server/address"), m_editAddress->text());
    s.setValue(QStringLiteral("server/port"), m_spinPort->value());
    s.setValue(QStringLiteral("server/maxClients"), m_spinMaxClients->value());
    s.setValue(QStringLiteral("server/auth"), m_chkAuth->isChecked());
    s.setValue(QStringLiteral("server/user"), m_editUser->text());
    s.setValue(QStringLiteral("server/pass"), m_editPass->text());
    log(QStringLiteral("配置已保存到 OpenSorceCar.ini"), 0);
}

void MainWindow::onLoadSettings()
{
    QSettings s(QStringLiteral("OpenSorceCar.ini"), QSettings::IniFormat);
    m_editAddress->setText(s.value(QStringLiteral("server/address"),
                                   QStringLiteral("0.0.0.0")).toString());
    m_spinPort->setValue(s.value(QStringLiteral("server/port"), 1883).toInt());
    m_spinMaxClients->setValue(s.value(QStringLiteral("server/maxClients"), 500).toInt());
    m_chkAuth->setChecked(s.value(QStringLiteral("server/auth"), false).toBool());
    m_editUser->setText(s.value(QStringLiteral("server/user")).toString());
    m_editPass->setText(s.value(QStringLiteral("server/pass")).toString());
}

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------
void MainWindow::log(const QString &text, int level)
{
    // 时间戳与 [信息]/[警告]/[错误] 前缀由日志面板统一处理
    if (m_logPanel)
        m_logPanel->appendLog(text, level);
}

void MainWindow::onLogLine(const QString &text, int level)
{
    log(text, level);
}

void MainWindow::onDbError(const QString &text)
{
    log(QStringLiteral("数据库：%1").arg(text), 2);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_db)
        m_db->flush();   // 退出前把缓冲落盘
    if (m_brokerThread && m_brokerThread->isRunning()) {
        QMetaObject::invokeMethod(m_broker, "stop", Qt::QueuedConnection);
        m_brokerThread->quit();
        m_brokerThread->wait(3000);
    }
    QMainWindow::closeEvent(event);
}
