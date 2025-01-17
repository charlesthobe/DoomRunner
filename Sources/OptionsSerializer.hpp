//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: serialization/deserialization of the user data into/from a file
//======================================================================================================================

#ifndef OPTIONS_INCLUDED
#define OPTIONS_INCLUDED


#include "Essential.hpp"

#include "UserData.hpp"

#include <QList>
#include <QString>


//======================================================================================================================

struct OptionsToSave
{
	// files
	const QList< EngineInfo > & engines;  // we must accept EngineInfo, but we will serialize only Engine fields
	const QList< IWAD > & iwads;

	// options
	const LaunchOptions & launchOpts;
	const MultiplayerOptions & multOpts;
	const GameplayOptions & gameOpts;
	const CompatibilityOptions & compatOpts;
	const VideoOptions & videoOpts;
	const AudioOptions & audioOpts;
	const GlobalOptions & globalOpts;

	// presets
	const QList< Preset > & presets;
	int selectedPresetIdx;

	// global settings
	const EngineSettings & engineSettings;
	const IwadSettings & iwadSettings;
	const MapSettings & mapSettings;
	const ModSettings & modSettings;
	const LauncherSettings & settings;
	WindowGeometry geometry;
};

struct OptionsToLoad
{
	// files
	QList< EngineInfo > engines;  // we must accept EngineInfo, but we will load only Engine fields
	QList< IWAD > iwads;

	// options
	LaunchOptions & launchOpts;
	MultiplayerOptions & multOpts;
	GameplayOptions & gameOpts;
	CompatibilityOptions & compatOpts;
	VideoOptions & videoOpts;
	AudioOptions & audioOpts;
	GlobalOptions & globalOpts;

	// presets
	QList< Preset > presets;
	QString selectedPreset;

	// global settings
	EngineSettings & engineSettings;
	IwadSettings & iwadSettings;
	MapSettings & mapSettings;
	ModSettings & modSettings;
	LauncherSettings & settings;
	WindowGeometry geometry;
};

bool writeOptionsToFile( const OptionsToSave & opts, const QString & filePath );
bool readOptionsFromFile( OptionsToLoad & opts, const QString & filePath );


#endif // OPTIONS_INCLUDED
