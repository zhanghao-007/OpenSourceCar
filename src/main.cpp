#include "ui/MainWindow.h"
#include "mqtt/MqttTypes.h"
#include "mqtt/MqttBroker.h"

#include <QApplication>
#include <QFont>
#include <QStyle>
#include <QStyleFactory>
#include <QDir>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OpenSorceCar"));
    app.setApplicationDisplayName(QStringLiteral("小车通信后台 · MQTT 服务端"));
    app.setOrganizationName(QStringLiteral("OpenSorceCar"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // 跨线程信号槽所用的自定义类型必须先注册
    qRegisterMetaType<Mqtt::MessageRecord>("Mqtt::MessageRecord");
    qRegisterMetaType<Mqtt::ClientViewInfo>("Mqtt::ClientViewInfo");
    qRegisterMetaType<Mqtt::BrokerStats>("Mqtt::BrokerStats");
    qRegisterMetaType<QList<Mqtt::ClientViewInfo> >("QList<Mqtt::ClientViewInfo>");

    // 中文字体与界面风格
    QFont font(QStringLiteral("Microsoft YaHei"), 9);
    app.setFont(font);
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);

    MainWindow w;
    w.resize(1280, 800);
    w.show();

    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (!args.isEmpty())
        w.applyCommandLine(args);

    return app.exec();
}
