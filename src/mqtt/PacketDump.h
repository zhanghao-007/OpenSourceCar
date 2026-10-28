#ifndef PACKETDUMP_H
#define PACKETDUMP_H

#include "MqttTypes.h"

#include <QString>
#include <QByteArray>

namespace Mqtt {

// ---------------------------------------------------------------------------
// 报文转储工具
//
// 把 TCP 上收发的 MQTT 原始报文渲染成可读的十六进制形式，用于运行日志与协议排查。
// 本模块只依赖 MqttTypes.h，可独立编译。
// ---------------------------------------------------------------------------

// 报文类型 -> 名称；未知类型返回 "UNKNOWN(数值)"
QString packetTypeName(int type);

// 固定头标志位文字描述：
//   PUBLISH      -> "DUP=0 QoS=1 RETAIN=0"
//   PUBREL/SUBSCRIBE/UNSUBSCRIBE -> 显示实际值，若不等于规范要求的 0x02 则标注「非法」
//   其它         -> "FLAGS=0x0N"
QString packetFlagsText(int type, quint8 flags);

// 多行十六进制转储，带偏移量与 ASCII 栏，形如：
//   0000  32 0C 00 04 4D 51 54 54  04 02 00 3C 00 07  |2...MQTT...<..|
//   0008  43 41 52 2D 30 30 31                        |CAR-001       |
// 末尾不带换行；超过 maxBytes 时追加一行截断提示
QString hexDump(const QByteArray &data, int maxBytes = 256, int perLine = 16);

// 单行紧凑形式："32 0C 00 04 4D 51 54 54 …（共 14 字节）"
QString hexSummary(const QByteArray &data, int maxBytes = 32);

// 从固定头首字节拆出报文类型与标志位
int    typeOf(quint8 header);
quint8 flagsOf(quint8 header);

} // namespace Mqtt

#endif // PACKETDUMP_H
