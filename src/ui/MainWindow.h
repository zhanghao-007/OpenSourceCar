#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "../mqtt/MqttTypes.h"
#include "../mqtt/MqttBroker.h"
#include "../core/DatabaseManager.h"

#include <QMainWindow>
#include <QThread>

QT_BEGIN_NAMESPACE
class QTabWidget;
class QLabel;
class QPushButton;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QAction;
class QTimer;
QT_END_NAMESPACE

class ClientsWidget;
class MessagesWidget;
class CommandWidget;
class TelemetryWidget;
class LogPanelWidget;
class HistoryWidget;
class CarRegistry;
class DatabaseManager;

// ---------------------------------------------------------------------------
// MainWindow —— 小车通信后台主窗口
//
// 线程划分：
//   * m_brokerThread + m_broker  —— MQTT 服务端，独立工作线程
//   * 本对象与全部 Widget、CarRegistry —— UI 线程
//   两者之间只通过 Qt 信号（跨线程自动排队）传递**数据拷贝**。
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // 命令行参数：--port <n>  --address <ip>  --autostart  --logfile <路径>
    void applyCommandLine(const QStringList &args);
    // 运行日志落盘（转发给日志面板）；空路径表示关闭
    bool setLogFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartStopClicked();
    void onBrokerStarted(quint16 port, const QString &address);
    void onBrokerStopped();
    void onBrokerError(const QString &text);
    void onLogLine(const QString &text, int level);

    void onClientConnected(const Mqtt::ClientViewInfo &info);
    void onClientDisconnected(const QString &clientId, const QString &reason);
    void onClientsSnapshot(const QList<Mqtt::ClientViewInfo> &list);
    void onMessageRecord(const Mqtt::MessageRecord &rec);
    void onStatsUpdated(const Mqtt::BrokerStats &stats);
    void onPacketSniffed(const QString &clientId, const QString &direction,
                         const QByteArray &raw);
    void onDbError(const QString &text);

    void onPublishCommand(const QString &topic, const QByteArray &payload,
                          Mqtt::QoS qos, bool retain);
    void onKickClient(const QString &clientId);
    void onSaveSettings();
    void onLoadSettings();

private:
    void buildUi();
    void buildMenuBar();
    void buildToolBar();
    void buildStatusBar();
    void connectBrokerSignals();
    void log(const QString &text, int level = 0);
    void setRunningState(bool running);
    void updateStatusBar();

private:
    // ---- MQTT 服务端 ----
    QThread          *m_brokerThread = nullptr;
    Mqtt::MqttBroker *m_broker       = nullptr;
    CarRegistry      *m_registry     = nullptr;
    DatabaseManager  *m_db           = nullptr;

    // ---- 界面 ----
    QTabWidget       *m_tabs         = nullptr;
    ClientsWidget    *m_clientsWidget   = nullptr;
    MessagesWidget   *m_messagesWidget  = nullptr;
    CommandWidget    *m_commandWidget   = nullptr;
    TelemetryWidget  *m_telemetryWidget = nullptr;

    // 顶部控制条
    QLineEdit   *m_editAddress = nullptr;
    QSpinBox    *m_spinPort    = nullptr;
    QPushButton *m_btnStart    = nullptr;
    QCheckBox   *m_chkAuth     = nullptr;
    QLineEdit   *m_editUser    = nullptr;
    QLineEdit   *m_editPass    = nullptr;
    QSpinBox    *m_spinMaxClients = nullptr;
    QLabel      *m_labState    = nullptr;

    LogPanelWidget *m_logPanel = nullptr;
    HistoryWidget  *m_historyWidget = nullptr;
    QTimer         *m_uiTimer  = nullptr;

    // 状态栏
    QLabel *m_labClients  = nullptr;
    QLabel *m_labMessages = nullptr;
    QLabel *m_labDevice   = nullptr;   // 设备链路解析计数
    QLabel *m_labUptime   = nullptr;
    QLabel *m_labListen   = nullptr;

    // ---- 本地计数 ----
    bool       m_running = false;
    qint64     m_msgIn   = 0;
    qint64     m_msgOut  = 0;
    qint64     m_devOk   = 0;    // 成功解析并落库的设备帧
    qint64     m_devFail = 0;    // 解析失败的设备帧
    QString    m_devTopicPrefix = QStringLiteral("device/link");  // 设备链路上报主题前缀
    QDateTime  m_startedAt;
};

#endif // MAINWINDOW_H
