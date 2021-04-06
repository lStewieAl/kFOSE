#include <filesystem>
#include "utility.h"
#include "commands_animation.h"
#include "file_animations.h"
#include "json.h"
#include <fstream>
#include <utility>

#include "GameData.h"
#include "GameAPI.h"
#include "GameRTTI.h"

void LoadPathsForList(const BGSListForm* listForm, const std::filesystem::path& path, bool firstPerson);

template <typename T>
void LoadPathsForType(const std::filesystem::path& path, const T identifier, bool firstPerson)
{
	auto append = false;
	for (std::filesystem::recursive_directory_iterator iter(path), end; iter != end; ++iter)
	{
		if (_stricmp(iter->path().extension().string().c_str(), ".kf") != 0)
			continue;
		const auto fullPath = iter->path().string();
		auto str = fullPath.substr(fullPath.find("AnimGroupOverride\\"));
		Log("Loading animation path " + str + "...");
		try
		{
			if constexpr (std::is_same<T, const TESObjectWEAP*>::value)
				OverrideWeaponAnimation(identifier, str, firstPerson, true, append);
			else if constexpr (std::is_same<T, const Actor*>::value)
				OverrideActorAnimation(identifier, str, firstPerson, true, append);
			else if constexpr (std::is_same<T, UInt8>::value)
				OverrideModIndexAnimation(identifier, str, firstPerson, true, append);
			else if constexpr (std::is_same<T, const BGSListForm*>::value)
				LoadPathsForList(identifier, path, firstPerson);
			else
				static_assert(false);
		}
		catch (std::exception& e)
		{
			Log(FormatString("AnimGroupOverride Error: %s", e.what()));
		}
		append = true;
	}
}

void LogForm(const TESForm* form)
{
	Log(FormatString("Detected in-game form %X %s %s", form->refID, form->GetName(), form->GetFullName() ? form->GetFullName()->name.CStr() : "<no name>"));
}

void LoadPathsForList(const BGSListForm* listForm, const std::filesystem::path& path, bool firstPerson)
{
	for (auto iter = listForm->list.Begin(); !iter.End(); ++iter)
	{
		LogForm(*iter);
		if (const auto* weapon = DYNAMIC_CAST(*iter, TESForm, TESObjectWEAP))
			LoadPathsForType(path, weapon, firstPerson);
		else if (const auto* actor = DYNAMIC_CAST(*iter, TESForm, Actor))
			LoadPathsForType(path, actor, firstPerson);
		else
			Log(FormatString("Unsupported type for list item %X", iter->refID));
	}
}

template <typename T>
void LoadPathsForPOV(const std::filesystem::path& path, const T identifier)
{
	for (const auto& pair : {std::make_pair("\\_male", false), std::make_pair("\\_1stperson", true)})
	{
		auto iterPath = path.string() + std::string(pair.first);
		if (std::filesystem::exists(iterPath))
			LoadPathsForType(iterPath, identifier, pair.second);
	}
}

void LoadModAnimPaths(const std::filesystem::path& path, const ModInfo* mod)
{
	LoadPathsForPOV<const UInt8>(path, mod->modIndex);
	for (std::filesystem::directory_iterator iter(path), end; iter != end; ++iter)
	{
		const auto& iterPath = iter->path();
		Log("Loading form ID " + iterPath.string());
		if (iter->is_directory())
		{
			
			try
			{
				const auto& folderName = iterPath.filename().string();
				const auto id = HexStringToInt(folderName);
				if (id != -1) 
				{
					const auto formId = (id & 0x00FFFFFF) + (mod->modIndex << 24);
					auto* form = LookupFormByID(formId);
					if (form)
					{
						LogForm(form);
						if (const auto* weapon = DYNAMIC_CAST(form, TESForm, TESObjectWEAP))
							LoadPathsForPOV(iterPath, weapon);
						else if (const auto* actor = DYNAMIC_CAST(form, TESForm, Actor))
							LoadPathsForPOV(iterPath, actor);
						else if (const auto* list = DYNAMIC_CAST(form, TESForm, BGSListForm))
							LoadPathsForPOV(iterPath, list);
						else
							Log(FormatString("Unsupported form type for %X", form->refID));
					}
					else
					{
						Log(FormatString("kNVSE Animation: Form %X not found!", formId));
					}
				}
			}
			catch (std::exception&) {}
		}
		else
		{
			Log("Skipping as path is not a directory...");
		}
	}
}

struct JSONEntry
{
	const std::string folderName;
	const TESForm* form;

	JSONEntry(std::string folderName, const TESForm* form)
		: folderName(std::move(folderName)), form(form)
	{
	}
};

std::vector<JSONEntry> g_jsonEntries;
std::unordered_map<std::string, std::filesystem::path> g_jsonFolders;

void HandleJson(const std::filesystem::path& path)
{
	Log("Reading from JSON file " + path.string());
	
	try
	{
		std::ifstream i(path);
		nlohmann::json j;
		i >> j;
		if (j.is_array())
		{
			for (auto& elem : j)
			{
				if (!elem.is_object())
				{
					Log("JSON error: expected object with mod, form and folder fields");
					continue;
				}
				auto modName = elem["mod"].get<std::string>();
				auto formIdStr = elem["form"].get<std::string>();
				auto folder = elem["folder"].get<std::string>();
				const auto* mod = DataHandler::Get()->LookupModByName(modName.c_str());
				if (!mod)
				{
					Log("Mod name " + modName + " was not found");
					continue;
				}
				auto formId = HexStringToInt(formIdStr);
				if (formId == -1)
				{
					Log("Form field was incorrectly formatted, got " + formIdStr);
					continue;
				}
				formId = (mod->modIndex << 24) + (formId & 0x00FFFFFF);
				auto* form = LookupFormByID(formId);
				if (!form)
				{
					Log(FormatString("Form %X was not found", formId));
					continue;
				}
				LogForm(form);
				Log(FormatString("Registered form %X for folder %s", formId, folder.c_str()));
				g_jsonEntries.emplace_back(folder, form);
			}
		}
		else
			Log(path.string() + " does not start as a JSON array");
	}
	catch (nlohmann::json::exception& e)
	{
		Log(FormatString("JSON error: %s", e.what()));
		Console_Print("[%s] There was an error parsing JSON for AnimGroupOverride. Check kNVSE.log for more info.", path.filename().string().c_str());
	}
	
}

void LoadJsonEntries()
{
	for (const auto& entry : g_jsonEntries)
	{
		Log(FormatString("JSON: Loading animations for form %X in path %s", entry.form->refID, entry.folderName.c_str()));
		if (auto path = g_jsonFolders.find(entry.folderName); path != g_jsonFolders.end())
		{
			if (const auto* weapon = DYNAMIC_CAST(entry.form, TESForm, TESObjectWEAP))
			{
				LoadPathsForPOV(path->second, weapon);
			}
			else if (const auto* actor = DYNAMIC_CAST(entry.form, TESForm, Actor))
			{
				LoadPathsForPOV(path->second, actor);
			}
			else
			{
				Log("JSON form error: form is neither weapon or actor");
				continue;
			}
			Log(FormatString("Loaded from JSON folder %s to form %X", path->second.string().c_str(), entry.form->refID));

		}
		else
			Log("Error: Could not find path to " + entry.folderName + " which appeared in a JSON file");
		
	}
	g_jsonEntries.clear();
	g_jsonFolders.clear();
}

void LoadFileAnimPaths()
{
	Log("Loading file anims");
	const auto dir = GetCurPath() + R"(\Data\Meshes\AnimGroupOverride)";
	if (std::filesystem::exists(dir))
	{
		for (std::filesystem::directory_iterator iter(dir.c_str()), end; iter != end; ++iter)
		{
			const auto& path = iter->path();
			const auto& fileName = path.filename();
			if (iter->is_directory())
			{
				Log(iter->path().string() + " found");
				
				const auto* mod = DataHandler::Get()->LookupModByName(fileName.string().c_str());
				
				if (mod)
					LoadModAnimPaths(path, mod);
				else if (_stricmp(fileName.extension().string().c_str(), ".esp") == 0 || _stricmp(fileName.extension().string().c_str(), ".esm") == 0)
					Log(FormatString("Mod with name %s is not loaded!", fileName.string().c_str()));
				else if (_stricmp(fileName.string().c_str(), "_male") != 0 && _stricmp(fileName.string().c_str(), "_1stperson") != 0)
				{
					g_jsonFolders.emplace(path.filename().string(), path);
					Log("Found anim folder " + fileName.string() + " which can be used in JSON");
				}
			}
			else if (_stricmp(path.extension().string().c_str(), ".json") == 0)
			{
				HandleJson(iter->path());
			}
		}
	}
	else
	{
		Log(dir + " does not exist.");
	}
	LoadJsonEntries();
}
