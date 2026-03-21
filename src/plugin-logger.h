/*
 * PluginLogger - 将插件运行日志写入 运行日志.md 文件
 * 
 * 日志文件路径：
 * C:\Program Files (x86)\Common Files\Adobe\CEP\extensions\VideoMarkerExtractor\obs\obs-named-chapter-hotkeys2\运行日志.md
 * 
 * 用法：
 *   plog(LOG_INFO, "something happened: %s", detail);
 *   plog(LOG_WARNING, "warning: %d", code);
 *   plog(LOG_ERROR, "error occurred");
 */

#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化日志系统，打开日志文件。在 obs_module_load 中调用 */
void plugin_logger_init(void);

/* 关闭日志系统，刷新并关闭文件。在 obs_module_unload 中调用 */
void plugin_logger_shutdown(void);

/* 写入一行日志（同时写入OBS日志和运行日志.md） */
void plugin_log_write(int log_level, const char *format, ...);

#ifdef __cplusplus
}
#endif

/*
 * plog 宏：同时输出到 OBS 日志和 运行日志.md 文件
 * 使用方式与 blog() 完全一致
 */
#define plog(level, format, ...) plugin_log_write(level, format, ##__VA_ARGS__)
