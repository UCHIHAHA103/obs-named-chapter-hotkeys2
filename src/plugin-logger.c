/*
 * PluginLogger 实现
 * 将插件运行日志写入 运行日志.md 文件，便于排查问题
 */

#include "plugin-logger.h"
#include <plugin-support.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#define PATH_SEP '/'
#endif

/* 日志文件路径 */
#define LOG_FILE_PATH "C:\\Program Files (x86)\\Common Files\\Adobe\\CEP\\extensions\\VideoMarkerExtractor\\obs\\obs-named-chapter-hotkeys2\\运行日志.md"

/* 日志级别名称 */
static const char *log_level_name(int level)
{
	switch (level) {
	case LOG_ERROR:
		return "ERROR";
	case LOG_WARNING:
		return "WARN ";
	case LOG_INFO:
		return "INFO ";
	case LOG_DEBUG:
		return "DEBUG";
	default:
		return "?????";
	}
}

/* 文件句柄（全局，启动时打开，关闭时释放） */
static FILE *g_log_file = NULL;
static int g_logger_initialized = 0;

/* 获取当前时间字符串: YYYY-MM-DD HH:MM:SS */
static void get_timestamp(char *buf, size_t buf_size)
{
	time_t now = time(NULL);
	struct tm tm_info;
#ifdef _WIN32
	localtime_s(&tm_info, &now);
#else
	localtime_r(&now, &tm_info);
#endif
	strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);
}


void plugin_logger_init(void)
{
	if (g_logger_initialized)
		return;

	/*
	 * 重要：使用二进制追加模式 "ab" 而非 "a, ccs=UTF-8"
	 * 原因：ccs=UTF-8 模式会将文件流设为宽字符模式，
	 *       此时必须用 fwprintf 而非 fprintf，否则会导致未定义行为/崩溃。
	 *       我们的字符串本身就是 UTF-8 编码，直接用 fprintf 二进制写入即可。
	 */
#ifdef _WIN32
	/* Windows: 使用宽字符路径打开以支持中文路径 */
	wchar_t wpath[1024];
	MultiByteToWideChar(CP_UTF8, 0, LOG_FILE_PATH, -1, wpath, 1024);
	g_log_file = _wfopen(wpath, L"ab");
#else
	g_log_file = fopen(LOG_FILE_PATH, "ab");
#endif

	g_logger_initialized = 1;

	if (!g_log_file) {
		blog(LOG_ERROR,
		     "[PluginLogger] Failed to open log file: %s",
		     LOG_FILE_PATH);
		return;
	}

	/* 写入 UTF-8 BOM（仅当文件为空时） */
	fseek(g_log_file, 0, SEEK_END);
	long file_size = ftell(g_log_file);
	if (file_size == 0) {
		/* 写入 UTF-8 BOM: EF BB BF */
		unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
		fwrite(bom, 1, 3, g_log_file);
	}

	/* 写入启动分隔线 */
	char time_buf[32];
	get_timestamp(time_buf, sizeof(time_buf));

	fprintf(g_log_file,
		"\n---\n\n"
		"## 插件启动 - %s\n\n"
		"| 时间 | 级别 | 消息 |\n"
		"|------|------|------|\n",
		time_buf);
	fflush(g_log_file);

	blog(LOG_INFO, "[PluginLogger] Log file opened: %s", LOG_FILE_PATH);
}

void plugin_logger_shutdown(void)
{
	if (!g_logger_initialized)
		return;

	if (g_log_file) {
		char time_buf[32];
		get_timestamp(time_buf, sizeof(time_buf));
		fprintf(g_log_file,
			"| %s | INFO  | [STOP] 插件已卸载 |\n\n",
			time_buf);
		fflush(g_log_file);
		fclose(g_log_file);
		g_log_file = NULL;
	}

	g_logger_initialized = 0;
}

void plugin_log_write(int log_level, const char *format, ...)
{
	char msg_buf[2048];
	va_list args;

	/* 格式化消息 */
	va_start(args, format);
	vsnprintf(msg_buf, sizeof(msg_buf), format, args);
	va_end(args);

	/* 1) 输出到 OBS 标准日志 */
	blog(log_level, "%s", msg_buf);

	/* 2) 输出到运行日志.md 文件 */
	if (g_log_file) {
		char time_buf[32];
		get_timestamp(time_buf, sizeof(time_buf));

		/* 转义消息中的 | 符号，防止破坏 Markdown 表格 */
		char escaped[4096];
		size_t j = 0;
		for (size_t i = 0; msg_buf[i] && j < sizeof(escaped) - 2; i++) {
			if (msg_buf[i] == '|') {
				escaped[j++] = '\\';
			}
			/* 将换行替换为 <br> */
			if (msg_buf[i] == '\n') {
				if (j + 4 < sizeof(escaped)) {
					escaped[j++] = '<';
					escaped[j++] = 'b';
					escaped[j++] = 'r';
					escaped[j++] = '>';
				}
				continue;
			}
			escaped[j++] = msg_buf[i];
		}
		escaped[j] = '\0';

		fprintf(g_log_file, "| %s | %s | %s |\n",
			time_buf, log_level_name(log_level), escaped);
		fflush(g_log_file);
	}
}
