#pragma once

// ============================================================================
// chapter-utils.hpp
//
// 独立的纯函数工具头（header-only），不依赖 OBS / Qt Widgets，仅依赖 Qt Core。
// 目的：
// 1. 便于作为纯函数集中维护颜色/时间码等可测试逻辑
// 2. 便于独立单元测试 target 使用（tests/test_chapter_utils.cpp）
//
// 使用：在 chapter-hotkeys.cpp 中 #include "chapter-utils.hpp"，
// 原有的 getColorName/getColorHex/formatTime 调用可以替换为这里的版本。
// ============================================================================

#include <QString>
#include <QMap>
#include <QChar>
#include <cstdint>

namespace chapter_utils {

// 颜色十六进制 -> 颜色英文名
inline QString colorHexToName(const QString &colorHex)
{
	if (colorHex == "#D22C36") return "red";
	if (colorHex == "#AF8BB1") return "purple";
	if (colorHex == "#E96F24") return "orange";
	if (colorHex == "#D0A12B") return "yellow";
	if (colorHex == "#FFFFFF") return "white";
	if (colorHex == "#428DFC") return "blue";
	if (colorHex == "#19F4D6") return "cyan";
	if (colorHex == "#718637") return "green";
	// 已经是名字就原样返回
	QString lower = colorHex.toLower();
	if (lower == "red" || lower == "purple" || lower == "orange" ||
	    lower == "yellow" || lower == "white" || lower == "blue" ||
	    lower == "cyan" || lower == "green")
		return lower;
	return "green";
}

// 颜色名或十六进制 -> 规范十六进制（以 # 开头）
inline QString colorNameToHex(const QString &nameOrHex)
{
	if (nameOrHex.startsWith('#')) {
		return nameOrHex.toLower();
	}
	static const QMap<QString, QString> map = {
		{"green",  "#718637"},
		{"red",    "#D22C36"},
		{"purple", "#AF8BB1"},
		{"orange", "#E96F24"},
		{"yellow", "#D0A12B"},
		{"white",  "#FFFFFF"},
		{"blue",   "#428DFC"},
		{"cyan",   "#19F4D6"},
	};
	return map.value(nameOrHex.toLower(), QString("#718637"));
}

// 毫秒 -> "HH:MM:SS"
inline QString formatTimeHMS(uint64_t ms)
{
	int totalSeconds = static_cast<int>(ms / 1000);
	int hours = totalSeconds / 3600;
	int minutes = (totalSeconds % 3600) / 60;
	int seconds = totalSeconds % 60;
	return QString("%1:%2:%3")
		.arg(hours, 2, 10, QChar('0'))
		.arg(minutes, 2, 10, QChar('0'))
		.arg(seconds, 2, 10, QChar('0'));
}

// 毫秒 -> 适合 YouTube 章节的紧凑时间码
// 小于 1 小时：M:SS（首数字无前导零）
// 大于等于 1 小时：H:MM:SS
inline QString formatTimeYouTube(uint64_t ms)
{
	int totalSeconds = static_cast<int>(ms / 1000);
	int hours = totalSeconds / 3600;
	int minutes = (totalSeconds % 3600) / 60;
	int seconds = totalSeconds % 60;
	if (hours > 0) {
		return QString("%1:%2:%3")
			.arg(hours)
			.arg(minutes, 2, 10, QChar('0'))
			.arg(seconds, 2, 10, QChar('0'));
	}
	return QString("%1:%2")
		.arg(minutes)
		.arg(seconds, 2, 10, QChar('0'));
}

// CSV 字段转义
inline QString csvEscapeField(const QString &field)
{
	QString s = field;
	s.replace('\r', ' ').replace('\n', ' ');
	bool needQuote = s.contains(',') || s.contains('"') || s.contains(';');
	if (needQuote) {
		s.replace('"', "\"\"");
		return '"' + s + '"';
	}
	return s;
}

// XML 文本转义
inline QString xmlEscapeText(const QString &text)
{
	QString s = text;
	s.replace('&', "&amp;")
	 .replace('<', "&lt;")
	 .replace('>', "&gt;")
	 .replace('"', "&quot;")
	 .replace('\'', "&apos;");
	return s;
}

} // namespace chapter_utils
