//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: logic of Engine Properties dialog that appears when you try to add of modify an engine
//======================================================================================================================

#include "EngineDialog.hpp"
#include "ui_EngineDialog.h"

#include "OwnFileDialog.hpp"
#include "Utils/WidgetUtils.hpp"
#include "Utils/OSUtils.hpp"
#include "Utils/MiscUtils.hpp"  // highlightInvalidPath
#include "Utils/ErrorHandling.hpp"

#include <QDir>
#include <QTimer>
#include <QMessageBox>
#include <QStandardPaths>


//======================================================================================================================

EngineDialog::EngineDialog( QWidget * parent, const PathContext & pathContext, const Engine & engine )
:
	QDialog( parent ),
	DialogCommon( this ),
	pathContext( pathContext ),
	engine( engine )
{
	ui = new Ui::EngineDialog;
	ui->setupUi(this);

	// automatically initialize family combox fox from existing engine families
	for (size_t familyIdx = 0; familyIdx < size_t(EngineFamily::_EnumEnd); ++familyIdx)
	{
		ui->familyCmbBox->addItem( familyToStr( EngineFamily(familyIdx) ) );
	}
	ui->familyCmbBox->setCurrentIndex( 0 );  // set this right at the start so that index is never -1

	// fill existing engine properties
	ui->nameLine->setText( engine.name );
	ui->pathLine->setText( engine.path );
	ui->configDirLine->setText( engine.configDir );
	ui->familyCmbBox->setCurrentIndex( int(engine.family) );

	// mark invalid paths
	highlightFilePathIfInvalid( ui->pathLine, engine.path  );
	highlightDirPathIfInvalid( ui->configDirLine, engine.configDir );

	connect( ui->browseEngineBtn, &QPushButton::clicked, this, &thisClass::browseEngine );
	connect( ui->browseConfigsBtn, &QPushButton::clicked, this, &thisClass::browseConfigDir );

	connect( ui->nameLine, &QLineEdit::textChanged, this, &thisClass::updateName );
	connect( ui->pathLine, &QLineEdit::textChanged, this, &thisClass::updatePath );
	connect( ui->configDirLine, &QLineEdit::textChanged, this, &thisClass::updateConfigDir );

	connect( ui->familyCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectFamily );

	// this will call the function when the window is fully initialized and displayed
	QTimer::singleShot( 0, this, &thisClass::onWindowShown );
}

void EngineDialog::adjustUi()
{
	// align the start of the line edits
	int maxLabelWidth = 0;
	if (int width = ui->nameLabel->width(); width > maxLabelWidth)
		maxLabelWidth = width;
	if (int width = ui->pathLabel->width(); width > maxLabelWidth)
		maxLabelWidth = width;
	if (int width = ui->configDirLabel->width(); width > maxLabelWidth)
		maxLabelWidth = width;
	if (int width = ui->familyLabel->width(); width > maxLabelWidth)
		maxLabelWidth = width;
	ui->nameLabel->setMinimumWidth( maxLabelWidth );
	ui->pathLabel->setMinimumWidth( maxLabelWidth );
	ui->configDirLabel->setMinimumWidth( maxLabelWidth );
	ui->familyLabel->setMinimumWidth( maxLabelWidth );
}

EngineDialog::~EngineDialog()
{
	delete ui;
}

void EngineDialog::showEvent( QShowEvent * event )
{
	// This can't be called in the constructor, because the widgets still don't have their final sizes there.
	adjustUi();

	superClass::showEvent( event );
}

void EngineDialog::onWindowShown()
{
	// This needs to be called when the window is fully initialized and shown, otherwise it will bug itself in a
	// half-shown state and not close properly.

	if (engine.path.isEmpty() && engine.name.isEmpty() && engine.configDir.isEmpty())
		browseEngine();

	if (engine.path.isEmpty() && engine.name.isEmpty() && engine.configDir.isEmpty())  // user closed the browseEngine dialog
		done( QDialog::Rejected );
}

static QString getEngineName( const QString & enginePath )
{
	// In Windows we can use the directory name, which can tell slightly more than just the binary,
	// but in Linux we have to fallback to the binary name (or use the Flatpak name if there is one).
	if (isWindows())
	{
		return getDirnameOfFile( enginePath );
	}
	else
	{
		ExecutableTraits traits = getExecutableTraits( enginePath );
		if (traits.sandboxEnv != Sandbox::None)
			return traits.sandboxAppName;
		else
			return traits.executableBaseName;
	}
}

static QString getEngineConfigDir( const QString & enginePath )
{
	// In Windows engines usually store their config in the directory of its binaries or in Saved Games,
	// but in Linux they store them in standard user's app config dir (usually something like /home/user/.config/)
	if (isWindows())
	{
		QString engineDir = getDirOfFile( enginePath );
		if (isDirectoryWritable( engineDir ))
		{
			return engineDir;
		}
		else  // if we cannot write to the directory of the executable (e.g. Program Files), try Saved Games
		{
			// this is not bullet-proof but will work for 90% of users
			return qEnvironmentVariable("USERPROFILE")%"/Saved Games/"%getFileNameFromPath( enginePath );
		}
	}
	else
	{
		ExecutableTraits traits = getExecutableTraits( enginePath );
		if (traits.sandboxEnv == Sandbox::Snap)
		{
			return getHomeDir()%"/snap/"%traits.executableBaseName%"/current/.config/"%traits.executableBaseName;
		}
		else if (traits.sandboxEnv == Sandbox::Flatpak)
		{
			return getHomeDir()%"/.var/app/"%traits.sandboxAppName%"/.config/"%traits.executableBaseName;
		}
		else
		{
			QDir standardConfigDir( QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation ) );
			QString appName = getFileNameFromPath( enginePath );
			return standardConfigDir.filePath( appName );  // -> /home/user/.config/zdoom
		}
	}
}

void EngineDialog::browseEngine()
{
	QString enginePath = OwnFileDialog::getOpenFileName( this, "Locate engine's executable", ui->pathLine->text(),
 #if IS_WINDOWS
		"Executable files (*.exe);;"
 #endif
		"All files (*)"
	);
	if (enginePath.isNull())  // user probably clicked cancel
		return;

	// the path comming out of the file dialog is always absolute
	if (pathContext.usingRelativePaths())
		enginePath = pathContext.getRelativePath( enginePath );

	ui->pathLine->setText( enginePath );

	if (ui->nameLine->text().isEmpty())  // don't overwrite existing name
		ui->nameLine->setText( getEngineName( enginePath ) );

	if (ui->configDirLine->text().isEmpty())  // don't overwrite existing config dir
		ui->configDirLine->setText( getEngineConfigDir( enginePath ) );

	// guess the engine family based on executable's name
	QString executableName = getFileBasenameFromPath( enginePath );
	EngineFamily guessedFamily = guessEngineFamily( executableName );
	ui->familyCmbBox->setCurrentIndex( int(guessedFamily) );
}

void EngineDialog::browseConfigDir()
{
	QString dirPath = OwnFileDialog::getExistingDirectory( this, "Locate engine's config directory", ui->configDirLine->text() );
	if (dirPath.isEmpty())  // user probably clicked cancel
		return;

	// the path comming out of the file dialog is always absolute
	if (pathContext.usingRelativePaths())
		dirPath = pathContext.getRelativePath( dirPath );

	ui->configDirLine->setText( dirPath );
}

void EngineDialog::updateName( const QString & text )
{
	engine.name = text;
}

void EngineDialog::updatePath( const QString & text )
{
	engine.path = text;

	highlightFilePathIfInvalid( ui->pathLine, text );
}

void EngineDialog::updateConfigDir( const QString & text )
{
	engine.configDir = text;

	highlightDirPathIfInvalid( ui->configDirLine, text );
}

void EngineDialog::selectFamily( int familyIdx )
{
	if (familyIdx < 0 || familyIdx >= int(EngineFamily::_EnumEnd))
	{
		reportBugToUser( this, "Invalid engine family index", "Family combo-box index is out of bounds." );
	}

	engine.family = EngineFamily( familyIdx );
}

void EngineDialog::accept()
{
	if (engine.name.isEmpty())
	{
		QMessageBox::warning( this, "Engine name is empty", "Please give the engine some name." );
		return;
	}
	if (engine.path.isEmpty())
	{
		QMessageBox::warning( this, "Executable path is empty", "Please specify the engine's executable path." );
		return;
	}
	if (isInvalidFile( engine.path ))
	{
		QMessageBox::warning( this, "Executable doesn't exist", "Please fix the engine's executable path, such file doesn't exist." );
		return;
	}
	if (isInvalidDir( engine.configDir ))
	{
		QMessageBox::warning( this, "Config dir doesn't exist", "Please fix the engine's config dir, such directory doesn't exist." );
		return;
	}

	// all problems fixed -> remove highlighting if it was there
	unhighlightListItem( engine );

	superClass::accept();
}
