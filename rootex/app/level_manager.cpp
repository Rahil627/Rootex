#include "level_manager.h"

#include "core/input/input_manager.h"
#include "framework/entity_factory.h"
#include "framework/systems/hierarchy_system.h"
#include "framework/systems/render_system.h"
#include "systems/audio_system.h"
#include "systems/serialization_system.h"

LevelDescription::LevelDescription()
    : m_LevelName("")
    , m_LevelSettingsFile(nullptr)
    , m_LevelSettings()
{
}

LevelDescription::LevelDescription(const String& levelPath)
{
	m_LevelName = FilePath(levelPath).filename().string();
	m_LevelSettingsFile = ResourceLoader::CreateTextResourceFile(levelPath + "/" + m_LevelName + ".level.json");
	m_LevelSettings = JSON::json::parse(m_LevelSettingsFile->getString());
	
	if (m_LevelSettings.find("preload") != m_LevelSettings.end())
	{
		for (auto& path : m_LevelSettings["preload"])
		{
			m_Preloads.push_back(path);
		}
	}
}

void LevelManager::RegisterAPI(sol::state& rootex)
{
	sol::usertype<Atomic<int>> atomicInt = rootex.new_usertype<Atomic<int>>("AtomicInt", sol::constructors<Atomic<int>(), Atomic<int>(int)>());
	atomicInt["load"] = [](Atomic<int>* a) { return a->load(); };
	
	sol::usertype<LevelManager> levelManager = rootex.new_usertype<LevelManager>("LevelManager");
	levelManager["Get"] = &LevelManager::GetSingleton;
	levelManager["openLevel"] = [](LevelManager* l, String& p) { return l->openLevel(p); };
	levelManager["preloadLevel"] = [](LevelManager* l, String& p, Atomic<int>& a) { return l->preloadLevel(p, a); };
	levelManager["openPreloadedLevel"] = [](LevelManager* l, String& p) { return l->openPreloadedLevel(p, false); };
}

LevelManager* LevelManager::GetSingleton()
{
	static LevelManager singleton;
	return &singleton;
}

int LevelManager::preloadLevel(const String& levelPath, Atomic<int>& progress, bool openInEditor)
{
	LevelDescription newLevel(levelPath);

	m_ToUnload.clear();
	for (auto& preloaded : m_CurrentLevel.getPreloads())
	{
		auto& findIt = std::find(newLevel.getPreloads().begin(), newLevel.getPreloads().end(), preloaded);
		if (findIt == newLevel.getPreloads().end())
		{
			m_ToUnload.push_back(preloaded);
		}
	}
	
	return ResourceLoader::Preload(newLevel.getPreloads(), progress);
}

void LevelManager::openLevel(const String& levelPath, bool openInEditor)
{
	if (isAnyLevelOpen())
	{
		endLevel();
	}

	Atomic<int> progress;
	int totalPreloads = preloadLevel(levelPath, progress, openInEditor);

	while (progress.load() != totalPreloads)
	{
		;
	}

	PRINT("Preloaded " + std::to_string(totalPreloads) + " new resources");

	openPreloadedLevel(levelPath, openInEditor);
}

void LevelManager::openPreloadedLevel(const String& levelPath, bool openInEditor)
{
	endLevel();

	m_CurrentLevel = LevelDescription(levelPath);

	ResourceLoader::Unload(m_ToUnload);

	if (!OS::IsExists(levelPath))
	{
		OS::CreateDirectoryName(levelPath);
	}
	if (!OS::IsExists(levelPath + "/entities/"))
	{
		OS::CreateDirectoryName(levelPath + "/entities/");
	}

	for (auto&& entityFile : OS::GetFilesInDirectory(levelPath + "/entities/"))
	{
		TextResourceFile* textResource = ResourceLoader::CreateTextResourceFile(entityFile.string());
		if (textResource->isDirty())
		{
			ResourceLoader::Reload(textResource);
		}

		EntityFactory::GetSingleton()->createEntity(textResource);
	}

	for (auto& [order, systems] : System::GetSystems())
	{
		for (auto& system : systems)
		{
			system->setConfig(m_CurrentLevel.getLevelSettings(), openInEditor);
		}
	}

	EntityFactory::GetSingleton()->setupLiveEntities();

	PRINT("Loaded level: " + levelPath);

	for (auto& [order, systems] : System::GetSystems())
	{
		for (auto& system : systems)
		{
			if (system->isActive())
			{
				system->begin();
			}
		}
	}
}

void LevelManager::saveCurrentLevel()
{
	SerializationSystem::GetSingleton()->saveAllEntities("game/assets/levels/" + m_CurrentLevel.getLevelName() + "/entities");
}

void LevelManager::saveCurrentLevelSettings()
{
	m_CurrentLevel.getLevelSettingsFile()->putString(m_CurrentLevel.getLevelSettings().dump(1, '\t'));
}

void LevelManager::createLevel(const String& newLevelName)
{
	OS::CreateDirectoryName("game/assets/levels/" + newLevelName);
	OS::CreateDirectoryName("game/assets/levels/" + newLevelName + "/entities/");

	JSON::json newLevelJSON;
	newLevelJSON["camera"] = ROOT_ENTITY_ID;
	newLevelJSON["inputSchemes"] = JSON::json::array();
	newLevelJSON["startScheme"] = "";
	newLevelJSON["listener"] = ROOT_ENTITY_ID;
	newLevelJSON["preload"] = JSON::json::array();
	OS::CreateFileName("game/assets/levels/" + newLevelName + "/" + newLevelName + ".level.json") << newLevelJSON.dump(1, '\t');

	PRINT("Created new level: " + "game/assets/levels/" + newLevelName);
}

void LevelManager::endLevel()
{
	if (isAnyLevelOpen())
	{
		for (auto& [order, systems] : System::GetSystems())
		{
			for (auto& system : systems)
			{
				system->end();
			}
		}

		EntityFactory::GetSingleton()->destroyEntities();
		HierarchySystem::GetSingleton()->getRootHierarchyComponent()->clear();

		PRINT("Ended level: " + m_CurrentLevel.getLevelSettingsFile()->getPath().generic_string());
	}
}
