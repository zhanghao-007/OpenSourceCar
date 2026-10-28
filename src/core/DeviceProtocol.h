#ifndef DEVICEPROTOCOL_H
#define DEVICEPROTOCOL_H

#include "../mqtt/MqttTypes.h"

#include <QString>
#include <QByteArray>
#include <QDateTime>

// ---------------------------------------------------------------------------
// 设备链路协议解析（device/link/req）
//
// 小车上报的报文统一放在固定主题 device/link/req（QoS 0），发布方 ClientID
// 形如 car_duayi_001，载荷是一层信封 + param 业务体：
//
//   {
//     "mid": 946700505,                      // 设备侧消息号（自增）
//     "type": "REP_DATA",                    // 报文类型
//     "timestamp": "2000-01-01 12:21:45",    // 设备本地时间（多为未对时的初值）
//     "devSn": "Car001",                     // 设备序列号
//     "param": {                             // 业务体，随 type 变化
//        "method": "SETTLEMENT",
//        "data": {"RSSI":0,"settlement_sensor":1.1,"AngX":0,"AngY":0,"BatVol":4.2}
//     }
//   }
//
// 已确认的 type / 字段：
//   REP_DATA（method=SETTLEMENT）  param.data: RSSI, settlement_sensor, AngX, AngY, BatVol
//   EVENT_LINKUP                  param: MAC, IPV4
//   EVENT_DORMANCY                param: reason
//
// 说明：
//   * timestamp 只作为"设备自报时间"原样留存，落库的检索时间一律用服务端
//     收到报文的时刻（设备未对时，会出现 2000-01-01 这类值）。
//   * 数值字段容错：number / "1.1" 字符串 / bool 都能解析，缺失按 0 处理，
//     这样个别固件把浮点写成字符串也不会整条丢弃。
// ---------------------------------------------------------------------------
namespace Dev {

enum FrameKind {
    KindUnknown  = 0,
    KindData     = 1,   // REP_DATA + method=SETTLEMENT
    KindLinkUp   = 2,   // EVENT_LINKUP
    KindDormancy = 3,   // EVENT_DORMANCY
    KindOther    = 4    // 未登记的类型，仍原样归档为事件
};

// 一帧解析结果
struct Frame {
    // ---- 信封 ----
    QString   topic;
    QString   clientId;              // 发布方 ClientID，如 car_duayi_001
    QDateTime recvAt;                // 服务端收到时刻
    qint64    mid      = 0;          // 设备消息号
    QString   devSn;                 // 设备序列号，如 Car001
    QString   type;                  // 报文类型
    QString   method;                // param.method，数据帧为 SETTLEMENT
    QString   devTs;                 // 设备原始时间字符串
    qint64    devTsMs  = 0;          // 设备时间可解析且在合理区间时才有值，否则 0

    FrameKind kind = KindUnknown;

    // ---- 数据帧（REP_DATA）----
    bool   hasData    = false;
    int    rssi       = 0;
    double settlement = 0.0;         // settlement_sensor，结算/沉降传感器读数
    double angX       = 0.0;         // 倾角 X（度）
    double angY       = 0.0;         // 倾角 Y（度）
    double batVol     = 0.0;         // 电池电压（V）

    // ---- 事件帧 ----
    QString mac;                     // EVENT_LINKUP
    QString ipv4;                    // EVENT_LINKUP
    QString reason;                  // EVENT_DORMANCY

    QByteArray payload;              // 原始载荷

    bool isValid() const { return !devSn.isEmpty() && !type.isEmpty(); }
};

// 主题是否属于设备链路；prefix 缺省 "device/link"
bool isDeviceTopic(const QString &topic,
                   const QString &prefix = QStringLiteral("device/link"));

// 解析一条 MQTT 消息；失败返回 false 并给出 error（中文，可直接进日志）
bool parse(const Mqtt::MessageRecord &rec, Frame &out, QString *error = nullptr);

// 类型名 -> FrameKind
FrameKind kindOf(const QString &type);

// 供日志/界面显示的短名
QString kindName(FrameKind k);

// 一帧的简短可读摘要，如 "Car001 SETTLEMENT bat=4.2V settlement=1.1 AngX=0 AngY=0 RSSI=0"
QString summary(const Frame &f);

} // namespace Dev

#endif // DEVICEPROTOCOL_H
