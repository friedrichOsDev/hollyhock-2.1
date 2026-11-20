#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/lcd.hpp>
#include <sdk/os/gui.hpp>
#include <sdk/os/file.hpp>
#include <sdk/os/mem.hpp>
#include <stddef.h>
#include <ui/ui.hpp>

inline void *operator new(size_t, void *memory) noexcept { return memory; }
inline void operator delete(void *, void *) noexcept {}

APP_NAME("UI File Manager Test")
APP_DESCRIPTION("Test the Hollyhock UI library file operations")
APP_AUTHOR("Friedrich")
APP_VERSION("1.0")

namespace {
enum { BTN_OK=1, BTN_CANCEL=2, BTN_INFO=10, BTN_DELETE=11, BTN_RENAME=12 };

int Len(const char *s) { int n=0; if(s) while(s[n]) ++n; return n; }
void Copy(char *dst,const char *src,int cap) { if(!dst||cap<=0)return; int i=0; if(src)while(src[i]&&i<cap-1){dst[i]=src[i];++i;}dst[i]=0; }
void Append(char *dst,const char *src,int cap) { int n=Len(dst); if(n<cap-1)Copy(dst+n,src,cap-n); }
void Join(const char *dir,const char *name,char *out,int cap) { Copy(out,dir,cap); int n=Len(out); if(n&&out[n-1]!='\\')Append(out,"\\",cap); Append(out,name,cap); }
void Wait() { Debug_SetCursorPosition(0,5); Debug_PrintString("Press any key...",false); LCD_Refresh(); Debug_WaitKey(); }
void Message(const char *title,const char *msg) { Debug_SetCursorPosition(0,0);Debug_PrintString(title,false);Debug_SetCursorPosition(0,1);Debug_PrintString(msg,false);LCD_Refresh();Wait(); }

class EventDialog : public GUIDialog {
 int ids[8],count,event;
public:
 EventDialog(const char *title,Height h=Height55):GUIDialog(h,AlignCenter,title,KeyboardStateNone),count(0),event(0){}
 void Register(int id){if(count<8)ids[count++]=id;}
 int Event(){return event;}
 int OnEvent(GUIDialog_Wrapped *d,GUIDialog_OnEvent_Data *e) override {
  if(e){int id=e->GetEventID();for(int i=0;i<count;i++)if(ids[i]==id){event=id;return GUIDialog::DialogResultOK;}}
  return GUIDialog::OnEvent(d,e);
 }
};
struct ElementStorage {
 void *p[8]; int n=0;
 ~ElementStorage(){for(int i=0;i<n;i++)free(p[i]);}
 template<class T> T *Make(){if(n>=8)return 0;void *m=malloc(sizeof(T));if(!m)return 0;p[n++]=m;return new(m)T;}
};
int ChooseAction(const char *path) {
 EventDialog d("File actions",GUIDialog::Height55); ElementStorage s;
 const int x=d.GetLeftX()+8,y=d.GetTopY()+8,w=(d.GetRightX()-d.GetLeftX()-16)/2,h=34;
 const char *labels[]={"Info / Read","Delete","Rename / Move","Cancel"};
 int ids[]={BTN_INFO,BTN_DELETE,BTN_RENAME,BTN_CANCEL};
 for(int i=0;i<4;i++){void *m=malloc(sizeof(GUIButton)); if(!m)continue; s.p[s.n++]=m;
  GUIButton *b=new(m) GUIButton(x+(i%2)*(w+4),y+(i/2)*(h+5),x+(i%2)*(w+4)+w,y+(i/2)*(h+5)+h,labels[i],ids[i],GUIButton::FlagEnabled);
  d.AddElement(*b);d.Register(ids[i]);
 }
 (void)path;d.ShowDialog();return d.Event();
}
bool Confirm(const char *title,const char *text) {
 EventDialog d(title,GUIDialog::Height35);ElementStorage s;
 const int x=d.GetLeftX()+8,y=d.GetBottomY()-40,w=64;
 const char *labels[]={"Yes","No"};int ids[]={BTN_OK,BTN_CANCEL};
 for(int i=0;i<2;i++){void *m=malloc(sizeof(GUIButton));if(!m)continue;s.p[s.n++]=m;
  GUIButton *b=new(m) GUIButton(x+i*(w+8),y,x+i*(w+8)+w,y+32,labels[i],ids[i],GUIButton::FlagEnabled);d.AddElement(*b);d.Register(ids[i]);}
 void *lm=malloc(sizeof(GUILabel));if(lm){s.p[s.n++]=lm;GUILabel *l=new(lm)GUILabel(d.GetLeftX()+8,d.GetTopY()+12,text);d.AddElement(*l);}
 d.ShowDialog();return d.Event()==BTN_OK;
}
bool Prompt(const char *title,const char *hint,char *out,int cap) {
 EventDialog d(title,GUIDialog::Height55);ElementStorage s;
 void *lm=malloc(sizeof(GUILabel));if(lm){s.p[s.n++]=lm;GUILabel *l=new(lm)GUILabel(d.GetLeftX()+8,d.GetTopY()+8,hint);d.AddElement(*l);}
 void *tm=malloc(sizeof(GUITextBox));if(!tm)return false;s.p[s.n++]=tm;
 GUITextBox *tb=new(tm)GUITextBox(d.GetLeftX()+8,d.GetTopY()+35,d.GetRightX()-d.GetLeftX()-16,cap-1,true);
 d.AddElement(*tb);
 int y=d.GetBottomY()-38,x=d.GetLeftX()+8;
 for(int i=0;i<2;i++){int id=i?BTN_CANCEL:BTN_OK;const char *label=i?"Cancel":"OK";void *m=malloc(sizeof(GUIButton));if(!m)continue;s.p[s.n++]=m;
  GUIButton *b=new(m)GUIButton(x+i*76,y,x+i*76+68,y+32,label,id,GUIButton::FlagEnabled);d.AddElement(*b);d.Register(id);}
 d.ShowDialog();if(d.Event()!=BTN_OK)return false;Copy(out,tb->GetText(),cap);return out[0]!=0;
}
void ShowInfo(const char *path) {
 struct stat st;int r=UI::FileSystem::Stat(path,&st);
 Debug_SetCursorPosition(0,0);Debug_PrintString("File information",false);
 Debug_SetCursorPosition(0,1);Debug_PrintString(path,false);
 Debug_SetCursorPosition(0,2);if(r==0)Debug_Printf(0,2,false,0,"Size: %u bytes",(unsigned)st.fileSize);else Debug_Printf(0,2,false,0,"stat error %d",r);
 Debug_SetCursorPosition(0,3);Debug_PrintString("Read preview (ASCII):",false);
 if(r==0){UI::File f;int o=f.Open(path,UI::FileRead);if(o>=0){char b[25];int n=f.Read(b,24);if(n>=0){b[n]=0;for(int i=0;i<n;i++)if((unsigned char)b[i]<32||(unsigned char)b[i]>126)b[i]='.';Debug_SetCursorPosition(0,4);Debug_PrintString(b,false);}f.Close();}}
 LCD_Refresh();Wait();
}
void Selected(UI::Application &app,const char *path){
 for(;;){int action=ChooseAction(path);if(action==BTN_CANCEL)break;
  if(action==BTN_INFO){ShowInfo(path);continue;}
  if(action==BTN_DELETE){if(Confirm("Confirm delete","Delete selected item?")){int r=::remove(path);Debug_SetCursorPosition(0,0);Debug_PrintString(r==0?"Deleted.":"Delete failed.",false);Debug_SetCursorPosition(0,1);Debug_Printf(0,1,false,0,"Result: %d",r);LCD_Refresh();Wait();}break;}
  if(action==BTN_RENAME){char dest[256];if(Prompt("Rename / move","Destination: fls0/path",dest,sizeof(dest))){char norm[256]={0};int r=UI::FileSystem::NormalizePath(dest,norm,sizeof(norm));if(r==0)r=::rename(path,norm);Debug_SetCursorPosition(0,0);Debug_PrintString(r==0?"Rename/move OK":"Rename/move failed",false);Debug_SetCursorPosition(0,1);Debug_Printf(0,1,false,0,"Result: %d",r);Debug_SetCursorPosition(0,2);Debug_PrintString(norm,false);LCD_Refresh();Wait();}break;}
 }
 app.ReturnToMenu();
}
void CreateFile(UI::Application &app){(void)app;char name[128];if(!Prompt("Create file","Path/name: fls0/...",name,sizeof(name)))return;char p[256];int r=UI::FileSystem::NormalizePath(name,p,sizeof(p));if(r==0){UI::File f;r=f.Open(p,UI::FileCreate);if(r>=0){const char text[]="Created by UI file-manager test.\r\n";int n=f.Write(text,sizeof(text)-1);r=n==(int)sizeof(text)-1?f.Close():(n<0?n:-1);}}Debug_SetCursorPosition(0,0);Debug_PrintString(r==0?"File created":"Create failed",false);Debug_SetCursorPosition(0,1);Debug_Printf(0,1,false,0,"Result: %d",r);LCD_Refresh();Wait();}
void CreateFolder(UI::Application &app){(void)app;char name[128];if(!Prompt("Create folder","Path/name: fls0/...",name,sizeof(name)))return;char p[256];int r=UI::FileSystem::NormalizePath(name,p,sizeof(p));if(r==0)r=::mkdir(p);Debug_SetCursorPosition(0,0);Debug_PrintString(r==0?"Folder created":"Create failed",false);Debug_SetCursorPosition(0,1);Debug_Printf(0,1,false,0,"Result: %d",r);LCD_Refresh();Wait();}
void DeletePath(UI::Application &app){(void)app;char path[256];if(!Prompt("Delete path","Path to file or folder",path,sizeof(path)))return;char norm[256]={0};int r=UI::FileSystem::NormalizePath(path,norm,sizeof(norm));if(r==0&&Confirm("Confirm delete","Remove this path?"))r=::remove(norm);Debug_SetCursorPosition(0,0);Debug_PrintString(r==0?"Delete completed":"Delete failed / cancelled",false);Debug_SetCursorPosition(0,1);Debug_Printf(0,1,false,0,"Result: %d",r);LCD_Refresh();Wait();}
void MovePath(UI::Application &app){(void)app;char source[256],destination[256];if(!Prompt("Move / rename","Source path",source,sizeof(source)))return;if(!Prompt("Move / rename","Destination path",destination,sizeof(destination)))return;char a[256]={0},b[256]={0};int r=UI::FileSystem::NormalizePath(source,a,sizeof(a));if(r==0)r=UI::FileSystem::NormalizePath(destination,b,sizeof(b));if(r==0)r=::rename(a,b);Debug_SetCursorPosition(0,0);Debug_PrintString(r==0?"Move / rename OK":"Operation failed",false);Debug_SetCursorPosition(0,1);Debug_Printf(0,1,false,0,"Result: %d",r);Debug_SetCursorPosition(0,2);Debug_PrintString(b,false);LCD_Refresh();Wait();}
void About(UI::Application &app){(void)app;Message("UI file manager test","Create, browse, read, delete, rename/move and mkdir.");}
}
extern "C" void main(){
 UI::Config c;c.title="UI File Manager Test";c.startLabel="Test overview";c.filesLabel="Browse / manage";c.settingsLabel="Operations";c.quitLabel="Exit";
 c.showStart=true;c.onStart=About;c.showFileSelector=true;c.onFileSelected=Selected;c.showSettings=true;c.showQuit=true;
 UI::Setting settings[]={UI::Setting::Action("Create file",CreateFile,"Create a text test file"),UI::Setting::Action("Create folder",CreateFolder,"Create a directory"),UI::Setting::Action("Delete path",DeletePath,"Delete a file or empty folder by path"),UI::Setting::Action("Move / rename path",MovePath,"Move or rename a file or folder")};
 c.settings=settings;c.settingCount=sizeof(settings)/sizeof(settings[0]);c.manageCalculatorLifecycle=true;
 UI::Application app(c);app.Run();
}
