#pragma once
namespace UI {
class Application;
typedef void (*ActionCallback)(Application &);
typedef void (*FileCallback)(Application &, const char *);
struct FileFilter { const char *extension; const char *description; FileFilter(const char *ext=0,const char *desc=0):extension(ext),description(desc){} };
enum SettingType { SettingBoolean, SettingInteger, SettingEnum, SettingAction };
struct Setting {
 const char *name,*description; SettingType type; void *value;
 int minimum,maximum,step; const char *const *options; int optionCount; ActionCallback action;
 Setting();
 static Setting Boolean(const char *,bool *,const char *description=0);
 static Setting Integer(const char *,int *,int minimum,int maximum,int step=1,const char *description=0);
 static Setting Enumeration(const char *,int *,const char *const *options,int optionCount,const char *description=0);
 static Setting Action(const char *,ActionCallback,const char *description=0);
};
struct Config {
 const char *title,*startLabel,*filesLabel,*settingsLabel,*quitLabel;
 bool showStart,showFileSelector,showSettings,showQuit,manageCalculatorLifecycle;
 ActionCallback onStart,onSettings; FileCallback onFileSelected;
 const FileFilter *filters; int filterCount; Setting *settings; int settingCount;
 Config();
};
bool FileFilterMatches(const char *filename,const FileFilter *filters,int filterCount);
}
