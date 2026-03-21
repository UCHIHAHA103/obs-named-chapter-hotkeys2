/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>
#include "plugin-logger.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

void InitChapterHotkeys(void);

bool obs_module_load(void)
{
	/* 先初始化日志系统，让后续所有操作都能被记录 */
	plugin_logger_init();
	
	plog(LOG_INFO, "Plugin loading... (version %s)", PLUGIN_VERSION);
	
	InitChapterHotkeys();
	
	plog(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	plog(LOG_INFO, "Plugin unloading...");
	
	/* 关闭日志系统（最后执行） */
	plugin_logger_shutdown();
}
