#ifndef COMMANDWIDGET_H
#define COMMANDWIDGET_H

#include "../mqtt/MqttTypes.h"

#include <QWidget>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QLabel;
class QCheckBox;
class QGridLayout;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// 指令下发页：向指定小车发布控制指令
// ---------------------------------------------------------------------------
class CommandWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CommandWidget(QWidget *parent = nullptr);

public slots:
    // 同步在线小车列表（来自 CarRegistry）
    void setCarIds(const QStringList &ids);
    void setTopicPrefix(const QString &prefix);

signals:
    void publishRequested(const QString &topic, const QByteArray &payload,
                          Mqtt::QoS qos, bool retain);

private slots:
    void onSendClicked();
    void onTargetChanged();
    void onTopicEdited();
    void onQuickCommand();
    void onClearHistory();
    void onNavToCoord();

private:
    void buildUi();
    void addQuickButton(QGridLayout *grid, int row, int col,
                        const QString &text, const QString &cmd,
                        const QString &params);
    void fillPayload(const QString &cmd, const QString &params);
    void updateTopicFromTarget();
    void appendHistory(const QString &topic, const QByteArray &payload,
                       Mqtt::QoS qos, bool retain);

    QComboBox      *m_cmbTarget   = nullptr;
    QComboBox      *m_cmbCoordSys = nullptr;
    QLineEdit      *m_editLat     = nullptr;
    QLineEdit      *m_editLon     = nullptr;
    QLineEdit      *m_editTopic   = nullptr;
    QPlainTextEdit *m_editPayload = nullptr;
    QComboBox      *m_cmbQos      = nullptr;
    QCheckBox      *m_chkRetain   = nullptr;
    QPushButton    *m_btnSend     = nullptr;
    QTableWidget   *m_history     = nullptr;
    QLabel         *m_labHint     = nullptr;

    QString    m_prefix = QStringLiteral("car");
    QString    m_cmdSuffix = QStringLiteral("cmd");
    int        m_seq = 1;
    bool       m_topicManual = false;   // 用户手改过主题后不再自动覆盖
};

#endif // COMMANDWIDGET_H
