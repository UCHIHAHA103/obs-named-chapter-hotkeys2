// ============================================================================
// test_chapter_utils.cpp
//
// chapter-utils.hpp 纯函数单元测试（Qt Test）
//
// 构建：默认不构建，需显式开启 -DBUILD_TESTING=ON
//   cmake -S . -B build -DBUILD_TESTING=ON
//   cmake --build build --target chapter-utils-tests
//   ctest --test-dir build --output-on-failure
// ============================================================================

#include <QtTest/QtTest>
#include "../src/chapter-utils.hpp"

using namespace chapter_utils;

class TestChapterUtils : public QObject {
	Q_OBJECT

private slots:
	// ===== 颜色转换 =====
	void colorHexToName_knownColors()
	{
		QCOMPARE(colorHexToName("#D22C36"), QString("red"));
		QCOMPARE(colorHexToName("#718637"), QString("green"));
		QCOMPARE(colorHexToName("#AF8BB1"), QString("purple"));
		QCOMPARE(colorHexToName("#E96F24"), QString("orange"));
		QCOMPARE(colorHexToName("#D0A12B"), QString("yellow"));
		QCOMPARE(colorHexToName("#FFFFFF"), QString("white"));
		QCOMPARE(colorHexToName("#428DFC"), QString("blue"));
		QCOMPARE(colorHexToName("#19F4D6"), QString("cyan"));
	}
	void colorHexToName_nameInput()
	{
		// 传入名字应原样返回
		QCOMPARE(colorHexToName("red"), QString("red"));
		QCOMPARE(colorHexToName("RED"), QString("red")); // 大小写不敏感
		QCOMPARE(colorHexToName("green"), QString("green"));
	}
	void colorHexToName_unknown()
	{
		// 未知颜色兜底绿色
		QCOMPARE(colorHexToName("#123456"), QString("green"));
		QCOMPARE(colorHexToName(""), QString("green"));
	}

	void colorNameToHex_knownColors()
	{
		QCOMPARE(colorNameToHex("red"),    QString("#D22C36"));
		QCOMPARE(colorNameToHex("GREEN"),  QString("#718637"));
		QCOMPARE(colorNameToHex("purple"), QString("#AF8BB1"));
	}
	void colorNameToHex_passThroughHex()
	{
		// 已经是 hex 应小写原样返回
		QCOMPARE(colorNameToHex("#D22C36"), QString("#d22c36"));
		QCOMPARE(colorNameToHex("#abcdef"), QString("#abcdef"));
	}
	void colorNameToHex_unknown()
	{
		QCOMPARE(colorNameToHex("unknown"), QString("#718637"));
		QCOMPARE(colorNameToHex(""),        QString("#718637"));
	}

	// ===== 时间码格式化 =====
	void formatTimeHMS_basic()
	{
		QCOMPARE(formatTimeHMS(0),          QString("00:00:00"));
		QCOMPARE(formatTimeHMS(999),        QString("00:00:00")); // 不满 1 秒
		QCOMPARE(formatTimeHMS(1000),       QString("00:00:01"));
		QCOMPARE(formatTimeHMS(59 * 1000),  QString("00:00:59"));
		QCOMPARE(formatTimeHMS(60 * 1000),  QString("00:01:00"));
		QCOMPARE(formatTimeHMS(3599 * 1000ULL), QString("00:59:59"));
		QCOMPARE(formatTimeHMS(3600 * 1000ULL), QString("01:00:00"));
		QCOMPARE(formatTimeHMS((3600 * 2 + 60 * 30 + 15) * 1000ULL),
			QString("02:30:15"));
	}

	void formatTimeYouTube_basic()
	{
		QCOMPARE(formatTimeYouTube(0),            QString("0:00"));
		QCOMPARE(formatTimeYouTube(5 * 1000),     QString("0:05"));
		QCOMPARE(formatTimeYouTube(75 * 1000),    QString("1:15"));
		QCOMPARE(formatTimeYouTube(10 * 60 * 1000ULL),   QString("10:00"));
		QCOMPARE(formatTimeYouTube(3600 * 1000ULL),      QString("1:00:00"));
		QCOMPARE(formatTimeYouTube((3600 + 125) * 1000ULL), QString("1:02:05"));
	}

	// ===== CSV 转义 =====
	void csvEscape_plain()
	{
		QCOMPARE(csvEscapeField("hello"),   QString("hello"));
		QCOMPARE(csvEscapeField("中文标记"), QString("中文标记"));
	}
	void csvEscape_withComma()
	{
		QCOMPARE(csvEscapeField("a,b"), QString("\"a,b\""));
	}
	void csvEscape_withQuote()
	{
		QCOMPARE(csvEscapeField("a\"b"), QString("\"a\"\"b\""));
	}
	void csvEscape_withNewline()
	{
		// 换行被替换为空格（不额外加引号，除非含分隔符）
		QCOMPARE(csvEscapeField("a\nb"),   QString("a b"));
		QCOMPARE(csvEscapeField("a,\nb"),  QString("\"a, b\""));
	}

	// ===== XML 转义 =====
	void xmlEscape_basic()
	{
		QCOMPARE(xmlEscapeText("a & b"), QString("a &amp; b"));
		QCOMPARE(xmlEscapeText("<x>"),   QString("&lt;x&gt;"));
		QCOMPARE(xmlEscapeText("say \"hi\""), QString("say &quot;hi&quot;"));
		QCOMPARE(xmlEscapeText("it's"),  QString("it&apos;s"));
	}
	void xmlEscape_combined()
	{
		// 确保 & 最先转义，不会被后续再次处理成 &amp;lt;
		QCOMPARE(xmlEscapeText("<&>"), QString("&lt;&amp;&gt;"));
	}
};

QTEST_MAIN(TestChapterUtils)
#include "test_chapter_utils.moc"
