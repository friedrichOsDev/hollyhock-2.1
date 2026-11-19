#pragma once
#include "types.hpp"
namespace UI {
class Application {
public:
 explicit Application(const Config &config); ~Application();
 Application(const Application &)=delete; Application &operator=(const Application &)=delete;
 void Run(); void Close(); void ReturnToMenu(); void OpenFileSelector(); void OpenSettings(); bool IsRunning() const;
private:
 enum State { StateMainMenu,StateFileSelector,StateSettings,StateExit };
 Config m_config; State m_state; char m_currentDirectory[256]; bool m_lifecycleStarted;
 void RunMainMenu(); void RunFileSelector(); void RunSettings();
};
}
