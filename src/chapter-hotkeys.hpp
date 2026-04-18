#pragma once

#include "ui_chapter-hotkeys.h"

#include "external/qt-wrappers.hpp"

#include <obs.hpp>
#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QListWidgetItem>
#include <QVariant>
#include <QComboBox>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QDockWidget>
#include <QListWidget>
#include <QJsonArray>
#include <QMutex>
#include <QAtomicInt>
#include <QTimer>
#include <string>
#include <memory>
#include <atomic>

Q_DECLARE_METATYPE(OBSDataArray);

// ============================================================================
// 配置方案 (Profile) 数据结构
// ============================================================================
struct MarkerProfile {
	QString name;          // 方案名称，如 "竞品分析方案"
	QString description;   // 方案描述
	QJsonArray markers;    // 标记列表
	bool enableComments;   // 是否启用注释
};

// ============================================================================
// 主 UI 对话框
// ============================================================================
class ChapterHotkeyUI : public QDialog {
	Q_OBJECT

	std::unique_ptr<Ui_HotkeyChaptersDialog> ui;

public:
	friend class ChapterWithCommentDialog;
	
	QCheckBox *enableCommentsCheckBox;
	QCheckBox *enableScreenToastCheckBox;
	ChapterHotkeyUI(QWidget *parent);

	void ShowHideDialog();

	void SaveHotkeys(obs_data_t *data);
	void LoadHotkeys(obs_data_t *data);
	void loadFromExternalConfig();
	bool IsCommentsEnabled();

	static QStringList GetAllChapterNames();
	
	// 恢复上次的方案选择状态（从外部配置文件读取）
	void restoreProfileSelection();
	
	// 配置方案管理
	void saveCurrentAsProfile(const QString &profileName, const QString &description = "");
	void loadProfile(const QString &profileName);
	void deleteProfile(const QString &profileName);
	QStringList getProfileNames();
	QString getCurrentProfileName() const { return currentProfileName; }
	void setCurrentProfileName(const QString &name) { currentProfileName = name; }
	
	// 导入/导出
	void exportConfig(const QString &filePath);
	void importConfig(const QString &filePath);
	
	// 确保 profiles 目录中至少有一个默认"预设"方案
	void ensureDefaultPreset();

private slots:
	void on_actionAddHotkey_triggered();
	void on_actionRemoveHotkey_triggered();
	void on_actionRenameHotkey_triggered();
	void on_colorButtonGreen_clicked();
	void on_colorButtonRed_clicked();
	void on_colorButtonPurple_clicked();
	void on_colorButtonOrange_clicked();
	void on_colorButtonYellow_clicked();
	void on_colorButtonWhite_clicked();
	void on_colorButtonBlue_clicked();
	void on_colorButtonCyan_clicked();
	
	// 导入导出按钮
	void onExportClicked();
	void onImportClicked();
	
	// 配置方案按钮
	void onProfileComboChanged(int index);
	void onSaveProfileClicked();
	void onDeleteProfileClicked();
	void onRenameProfileClicked();
	void onRefreshHotkeysClicked();

private:
	void setSelectedItemColor(const QString &color);
	void saveToExternalConfig();
	QString getExternalConfigPath();
	QString getProfilesDir();
	void refreshProfileCombo();
	void autoSaveCurrentProfile();
	void refreshAllHotkeyDisplays();
	
	QComboBox *profileCombo = nullptr;
	QPushButton *saveProfileBtn = nullptr;
	QPushButton *deleteProfileBtn = nullptr;
	QPushButton *renameProfileBtn = nullptr;
	QPushButton *refreshHotkeysBtn = nullptr;
	QPushButton *exportBtn = nullptr;
	QPushButton *importBtn = nullptr;
	QString currentProfileName;
};

// ============================================================================
// 自定义列表项绘制代理 - 标记名称左对齐，快捷键右对齐灰色显示
// ============================================================================
class HotkeyItemDelegate : public QStyledItemDelegate {
	Q_OBJECT
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *painter, const QStyleOptionViewItem &option,
		   const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option,
		       const QModelIndex &index) const override;
};

// 专用于注释弹窗下拉框的绘制代理：同样左对齐名称、右对齐灰色快捷键，但不绘制列表分隔线
class ComboHotkeyItemDelegate : public QStyledItemDelegate {
	Q_OBJECT
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *painter, const QStyleOptionViewItem &option,
		   const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option,
		       const QModelIndex &index) const override;
};

enum HotkeyDataRoles { Name = Qt::UserRole, HotkeyId, Bindings, Color, HotkeyText };

// ============================================================================
// 快捷键列表项
// ============================================================================
class ChapterHotkeyItem : public QListWidgetItem {

public:
	ChapterHotkeyItem(const QString &id, const char *name,
			  obs_data_array_t *bindings, const QString &color = "green");
	~ChapterHotkeyItem() override;

	QVariant data(int role) const override;
	void setData(int role, const QVariant &value) override;
	
	QString getHotkeyText() const;
	void updateDisplayText();

	obs_hotkey_id getHotkey() const { return hotkey; }
	std::string getChapterName() const { return chapterName; }
	QString getColor() const { return color; }
	void setColor(const QString &newColor) { color = newColor; }
 
private:
	obs_hotkey_id hotkey;
	std::string chapterName;
	QString hotkeyUUID;
	QString color;
 
	static void HotkeyPressed(void *_this, obs_hotkey_id, obs_hotkey_t *,
				  bool pressed);
};

// ============================================================================
// 带注释的标记对话框 (改进重入保护)
// ============================================================================
class ChapterWithCommentDialog : public QDialog {
	Q_OBJECT

public:
	ChapterWithCommentDialog(QWidget *parent);
	~ChapterWithCommentDialog() override;

	static bool AskForNameAndComment(QWidget *parent, const QString &title,
					const QString &text, std::string &nameInput,
					std::string &commentInput,
					const QString &placeHolder = QString(""),
					QString *colorInput = nullptr);
	static bool IsDialogOpen() { return s_isDialogOpen.load(); }

protected:
	void closeEvent(QCloseEvent *event) override;
	bool eventFilter(QObject *obj, QEvent *event) override;

public:
	// 是否已从配置加载过历史位置（true 表示跳过自动居中）
	bool hasRestoredPosition() const { return m_positionRestored; }

private:
	QLabel *nameLabel;
	QLabel *commentLabel;
	QComboBox *nameCombo;
	QTextEdit *commentInput;
	void saveWindowState();
	void loadWindowState();
	
	bool m_positionRestored = false;
	static std::atomic<bool> s_isDialogOpen;
	static QMutex s_dialogMutex;
};

// ============================================================================
// 简单名称输入对话框
// ============================================================================
class ChapterNameDialog : public QDialog {
	Q_OBJECT

public:
	ChapterNameDialog(QWidget *parent);

	static bool AskForName(QWidget *parent, const QString &title,
			       const QString &text, std::string &name,
			       const QString &placeHolder = QString(""));

private:
	QLabel *label;
	QLineEdit *input;
};

// ============================================================================
// 实时标记预览面板 (OBS Dock Widget)
// ============================================================================
struct LiveMarkerEntry {
	int index;
	QString name;
	QString color;
	QString comment;
	QString timeCode;     // 格式 HH:MM:SS
	uint64_t timestampMs; // 录制中的毫秒数
};

class MarkerLivePanel : public QWidget {
	Q_OBJECT

public:
	MarkerLivePanel(QWidget *parent = nullptr);
	~MarkerLivePanel() override;

	void addMarker(const QString &name, const QString &color, const QString &comment);
	void clearMarkers();
	int markerCount() const { return markers.size(); }

private slots:
	void onRecordingStarted();
	void onRecordingStopped();
	void updateRecordingTime();

private:
	QListWidget *markerList;
	QLabel *statusLabel;
	QLabel *timerLabel;
	QPushButton *clearBtn;
	QPushButton *copyBtn;
	
	QList<LiveMarkerEntry> markers;
	QTimer *recordingTimer;
	uint64_t recordingStartTime = 0;
	bool isRecording = false;
	
	void refreshList();
	QString formatTime(uint64_t ms) const;
	void copyMarkersToClipboard();
};

// ============================================================================
// 屏幕 Toast - 无边框透明悬浮窗,在主屏幕左上角显示一段文本后自动淡出
// 不抢焦点、不影响游戏输入；用于按下标记热键后的视觉反馈
// ============================================================================
class ScreenToast : public QWidget {
	Q_OBJECT
public:
	// 全局单例入口：在主屏左上角显示 message，持续 durationMs 后淡出
	// 连续调用会复用同一窗口（替换文本，重置计时）
	// 注意：方法名避开基类 QWidget::show()，否则会隐藏基类重载
	static void showToast(const QString &message, int durationMs = 1800);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	explicit ScreenToast();
	void showMessage(const QString &message, int durationMs);
	void updatePosition();
	void startFadeOut();

	QString m_text;
	QTimer *m_hideTimer = nullptr;
	QTimer *m_fadeTimer = nullptr;
	double m_opacity = 0.95;
};
