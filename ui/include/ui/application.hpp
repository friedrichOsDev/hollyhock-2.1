#pragma once
#include "types.hpp"
#include "storage.hpp"
namespace UI {
class Application {
public:
 explicit Application(const Config &config); ~Application();
 Application(const Application &)=delete; Application &operator=(const Application &)=delete;
 void Run(); void Close(); void ReturnToMenu(); void OpenFileSelector(); void OpenSettings(); bool IsRunning() const;
 // Returns 0 on success, or the latest MCS persistence error code.
 int SettingsPersistenceResult() const;
private:
 enum State { StateMainMenu,StateFileSelector,StateSettings,StateExit };
 Config m_config; State m_state; char m_currentDirectory[256]; bool m_lifecycleStarted;
 PersistentStore m_settingsStore;
 bool m_persistenceInitialized;
 bool m_persistenceReady;
 int m_persistenceResult;
 void InitializeSettingsPersistence();
 void LoadSettingsFromStorage();
 void SaveSettingsToStorage();
 void RunMainMenu(); void RunFileSelector(); void RunSettings();
};
}
