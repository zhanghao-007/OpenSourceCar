#ifndef LOGPANELWIDGET_H
#define LOGPANELWIDGET_H

#include "../mqtt/MqttTypes.h"

#include <QWidget>
#include <QString>
#include <QFile>
#include <QTextStream>

QT_BEGIN_NAMESPACE
class QPlainTextEdit;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QTimer;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// 运行日志面板
//
// 既打印系统事件（连接 / 断开 / 协议错误），也打印 MQTT 消息本身。
// 小车遥测频率很高（每台每秒可达数十条），直接全打会把界面刷爆，
// 因此做了这些控制：
//   1. 方向过滤：默认只记录上行 IN，下行 OUT 可选
//   2. 每秒限流：默认每秒最多打印 20 条，超出的累计成一行"已省略 N 条"
//   3. 行数上限：默认保留 2000 行，超出自动丢弃最旧的
//   4. 载荷截断：默认截断到 200 字符，可勾选「完整载荷」看全量
//   5. 落盘：可把同样的内容以纯文本写入文件，便于事后排查
// 统计数字始终按真实条数累计，不受限流影响。
// ---------------------------------------------------------------------------
class LogPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogPanelWidget(QWidget *parent = nullptr);

    // ---- 日志落盘 ----
    // 设置落盘文件路径；path 为空则关闭落盘。返回是否成功打开。
    bool    setLogFile(const QString &path);
    QString logFilePath() const;
    bool    isLogFileEnabled() const;

public slots:
    // level: 0=信息 1=警告 2=错误
    void appendLog(const QString &text, int level);
    // 打印一条 MQTT 消息（方向过滤与限流在此生效）
    void appendMessage(const Mqtt::MessageRecord &rec);
    // 打印一个原始 MQTT 报文的十六进制转储
    void appendPacket(const QString &clientId, const QString &direction,
                      const QByteArray &raw);
    void clearLog();

private slots:
    void onClearClicked();
    void onExportClicked();
    void onOptionChanged();
    void onLogFileToggled(bool on);
    void onLogPathClicked();
    void onFlushTimer();

private:
    void buildUi();
    void writeLine(const QString &html);
    void flushSuppressed();
    QString escapePayload(const QByteArray &payload) const;   // 界面用：HTML
    QString plainPayload(const QByteArray &payload) const;    // 落盘用：纯文本
    void    appendToLogFile(const QString &plainText);

    QPlainTextEdit *m_logEdit      = nullptr;
    QCheckBox      *m_chkIn        = nullptr;
    QCheckBox      *m_chkOut       = nullptr;
    QCheckBox      *m_chkAutoScroll = nullptr;
    QCheckBox      *m_chkLogFile   = nullptr;
    QCheckBox      *m_chkFullPayload = nullptr;
    QCheckBox      *m_chkPacket    = nullptr;
    QSpinBox       *m_spinRate     = nullptr;
    QSpinBox       *m_spinMax      = nullptr;
    QPushButton    *m_btnClear     = nullptr;
    QPushButton    *m_btnExport    = nullptr;
    QPushButton    *m_btnLogPath   = nullptr;
    QLabel         *m_labStat      = nullptr;
    QLabel         *m_labLogPath   = nullptr;
    QTimer         *m_flushTimer   = nullptr;   // 兜底：消息停止后补出「已省略 N 条」

    QFile       m_logFile;        // 落盘文件（未启用时未打开）
    QTextStream m_logStream;
    QString     m_logFilePath;

    qint64  m_msgTotal   = 0;    // 累计收到的条数（不受限流影响）
    int     m_shown      = 0;    // 实际打印出来的条数
    int     m_secCount   = 0;    // 当前这一秒已打印的条数
    int     m_suppressed = 0;    // 当前这一秒被限流省略的条数
    int     m_lastSecond = -1;   // 上次打印所在的秒（0~59）
    QString m_lastSuppressedTopic;
};

#endif // LOGPANELWIDGET_H
