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
#include <string>
#include <memory>

Q_DECLARE_METATYPE(OBSDataArray);

class ChapterHotkeyUI : public QDialog {
	Q_OBJECT

	std::unique_ptr<Ui_HotkeyChaptersDialog> ui;

public:
	friend class ChapterWithCommentDialog;
	
	QCheckBox *enableCommentsCheckBox;
	ChapterHotkeyUI(QWidget *parent);

	void ShowHideDialog();

	void SaveHotkeys(obs_data_t *data);
	void LoadHotkeys(obs_data_t *data);
	bool IsCommentsEnabled();

	static QStringList GetAllChapterNames();

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

private:
	void setSelectedItemColor(const QString &color);
	void saveToExternalConfig();
	void loadFromExternalConfig();
	QString getExternalConfigPath();
};

enum HotkeyDataRoles { Name = Qt::UserRole, HotkeyId, Bindings, Color };

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

class ChapterWithCommentDialog : public QDialog {
	Q_OBJECT

public:
	ChapterWithCommentDialog(QWidget *parent);
	~ChapterWithCommentDialog() override;

	static bool AskForNameAndComment(QWidget *parent, const QString &title,
					const QString &text, std::string &nameInput,
					std::string &commentInput,
					const QString &placeHolder = QString(""));
	static bool IsDialogOpen() { return s_isDialogOpen; }

protected:
	void closeEvent(QCloseEvent *event) override;
	bool eventFilter(QObject *obj, QEvent *event) override;

private:
	QLabel *nameLabel;
	QLabel *commentLabel;
	QComboBox *nameCombo;
	QTextEdit *commentInput;
	void saveWindowState();
	void loadWindowState();
	
	static bool s_isDialogOpen;
};

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
