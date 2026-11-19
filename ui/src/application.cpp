#include <ui/application.hpp>
#include <sdk/calc/calc.hpp>
#include <sdk/os/gui.hpp>
#include <sdk/os/file.hpp>
#include <sdk/os/mem.hpp>
#include <stdint.h>
#include <stddef.h>

// Minimal placement-new support for this freestanding toolchain.
inline void *operator new(size_t, void *memory) noexcept { return memory; }
inline void operator delete(void *, void *) noexcept {}
namespace UI {
namespace {
enum { E_START=1,E_FILES=2,E_SETTINGS=3,E_QUIT=4,E_BACK=5,E_PREV=6,E_NEXT=7,E_UP=8,E_ROW=20,ROWS=5,MAX_ENTRIES=64,NAME_LEN=128,PATH_LEN=256 };
class EventDialog:public GUIDialog{
public:explicit EventDialog(const char*t):GUIDialog(Height95,AlignCenter,t,KeyboardStateNone),event(0){}
 int EventID()const{return event;}
 int OnEvent(GUIDialog_Wrapped*d,GUIDialog_OnEvent_Data*e)override{
  if(!e)return GUIDialog::OnEvent(d,e);
  event=e->GetEventID();
  // Custom UI buttons should close the dialog so ShowDialog() returns.
  // The caller then dispatches the captured event ID.
  if(event!=0)return DialogResultOK;
  return GUIDialog::OnEvent(d,e);
 }
private:int event;
};
class Elements{
 void*mem[12];int used;
public:Elements():used(0){}
 ~Elements(){for(int i=0;i<used;++i)free(mem[i]);}
 void AddButton(EventDialog&d,uint16_t x,uint16_t y,uint16_t r,uint16_t b,const char*t,int id,bool enabled=true){
  if(used>=12)return;void*p=malloc(sizeof(GUIButton));if(!p)return;
  GUIButton*q=new(p)GUIButton(x,y,r,b,t,(uint16_t)id,enabled?GUIButton::FlagEnabled:0);mem[used++]=p;d.AddElement(*q);
 }
 void AddLabel(EventDialog&d,int x,int y,const char*t){
  if(used>=12)return;void*p=malloc(sizeof(GUILabel));if(!p)return;
  GUILabel*q=new(p)GUILabel(x,y,t);mem[used++]=p;d.AddElement(*q);
 }
};
int Len(const char*s){int n=0;if(s)while(s[n])++n;return n;}
void Copy(char*d,const char*s,int cap){if(!d||cap<=0)return;int i=0;if(s)while(s[i]&&i<cap-1){d[i]=s[i];++i;}d[i]=0;}
void Append(char*d,const char*s,int cap){int n=Len(d);if(n<cap-1)Copy(d+n,s,cap-n);}
void Number(int v,char*out,int cap){if(cap<=0)return;char rev[16];int n=0;bool neg=v<0;unsigned int u=neg?(unsigned int)(-(v+1))+1U:(unsigned int)v;do{rev[n++]=char('0'+u%10U);u/=10U;}while(u&&n<(int)sizeof(rev));int p=0;if(neg&&p<cap-1)out[p++]='-';while(n&&p<cap-1)out[p++]=rev[--n];out[p]=0;}
char Lower(char c){return c>='A'&&c<='Z'?char(c+32):c;}
bool Equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b){if(Lower(*a)!=Lower(*b))return false;++a;++b;}return *a==*b;}
void WideToAscii(const wchar_t*s,char*d,int cap){int i=0;if(s)while(s[i]&&i<cap-1){unsigned int c=(unsigned int)s[i];d[i]=(c>=32&&c<=126)?char(c):'?';++i;}d[i]=0;}
void AsciiToWide(const char*s,wchar_t*d,int cap){int i=0;if(s)while(s[i]&&i<cap-1){d[i]=(wchar_t)(unsigned char)s[i];++i;}d[i]=0;}
struct Entry{char name[NAME_LEN];bool dir;};
int Compare(const char*a,const char*b){while(*a&&*b){char x=Lower(*a),y=Lower(*b);if(x!=y)return x<y?-1:1;++a;++b;}return *a==*b?0:(*a?1:-1);}
void Sort(Entry*e,int n){for(int i=1;i<n;++i){Entry v=e[i];int j=i;while(j>0){Entry&p=e[j-1];if(p.dir&&!v.dir)break;if(p.dir==v.dir&&Compare(p.name,v.name)<=0)break;e[j]=e[j-1];--j;}e[j]=v;}}
void Scan(const char*dir,const Config&cfg,Entry*e,int*count){
 *count=0;wchar_t search[PATH_LEN];AsciiToWide(dir,search,PATH_LEN);int n=Len(dir);if(n+2>=PATH_LEN)return;search[n]=L'*';search[n+1]=0;
 int handle=-1;wchar_t name[NAME_LEN];findInfo info;int result=findFirst(search,&handle,name,&info);if(result<0)return;
 while(result==0){char narrow[NAME_LEN];WideToAscii(name,narrow,sizeof(narrow));bool skip=Equal(narrow,".")||Equal(narrow,"..");bool directory=info.type==findInfo::EntryTypeDirectory;
  if(!skip&&(directory||FileFilterMatches(narrow,cfg.filters,cfg.filterCount))&&*count<MAX_ENTRIES){Copy(e[*count].name,narrow,NAME_LEN);e[*count].dir=directory;++*count;}
  result=findNext(handle,name,&info);
 }findClose(handle);Sort(e,*count);
}
void Bounds(GUIDialog&d,int row,uint16_t*x,uint16_t*y,uint16_t*r,uint16_t*b){*x=d.GetLeftX()+8;*r=d.GetRightX()-8;*y=d.GetTopY()+8+row*42;*b=*y+35;}
void Button(Elements&el,EventDialog&d,int row,const char*text,int id,bool enabled=true){uint16_t x,y,r,b;Bounds(d,row,&x,&y,&r,&b);el.AddButton(d,x,y,r,b,text,id,enabled);}
void Bottom(Elements&el,EventDialog&d,int x,int w,const char*t,int id){int y=d.GetBottomY()-40;el.AddButton(d,d.GetLeftX()+x,y,d.GetLeftX()+x+w,y+35,t,id);}
void PathLabel(Elements&el,EventDialog&d,const char*p){el.AddLabel(d,d.GetLeftX()+8,d.GetTopY()+2,p);}
void Join(const char*dir,const char*name,char*out,int cap){Copy(out,dir,cap);int n=Len(out);if(n&&out[n-1]!='\\')Append(out,"\\",cap);Append(out,name,cap);}
void Parent(char*p){const char*root="\\\\fls0\\";int rootn=Len(root),n=Len(p);if(n<=rootn)return;if(p[n-1]=='\\')--n;while(n>rootn&&p[n-1]!='\\')--n;if(n<rootn)n=rootn;p[n]=0;}
void Value(const Setting&s,char*out,int cap){out[0]=0;if(!s.value){Copy(out,"(unset)",cap);return;}if(s.type==SettingBoolean)Copy(out,*static_cast<bool*>(s.value)?"On":"Off",cap);else if(s.type==SettingInteger)Number(*static_cast<int*>(s.value),out,cap);else if(s.type==SettingEnum){int v=*static_cast<int*>(s.value);if(s.options&&v>=0&&v<s.optionCount)Copy(out,s.options[v],cap);else Number(v,out,cap);}else Copy(out,"Run",cap);}
void Adjust(Setting&s,Application&a){if(s.type==SettingAction){if(s.action)s.action(a);return;}if(!s.value)return;if(s.type==SettingBoolean){bool*v=(bool*)s.value;*v=!*v;}else if(s.type==SettingInteger){int*v=(int*)s.value;int next=*v+s.step;if(next>s.maximum)next=s.minimum;if(next<s.minimum)next=s.minimum;*v=next;}else if(s.type==SettingEnum&&s.optionCount>0){int*v=(int*)s.value;++*v;if(*v>=s.optionCount||*v<0)*v=0;}}
}
Application::Application(const Config&c):m_config(c),m_state(StateMainMenu),m_lifecycleStarted(false){Copy(m_currentDirectory,"\\\\fls0\\",sizeof(m_currentDirectory));}
Application::~Application(){if(m_lifecycleStarted&&m_config.manageCalculatorLifecycle){calcEnd();m_lifecycleStarted=false;}}
void Application::Run(){if(m_state==StateExit)return;if(m_config.manageCalculatorLifecycle&&!m_lifecycleStarted){calcInit();m_lifecycleStarted=true;}
 while(m_state!=StateExit){switch(m_state){case StateMainMenu:RunMainMenu();break;case StateFileSelector:RunFileSelector();break;case StateSettings:RunSettings();break;default:m_state=StateExit;break;}}
 if(m_lifecycleStarted&&m_config.manageCalculatorLifecycle){calcEnd();m_lifecycleStarted=false;}}
void Application::Close(){m_state=StateExit;}
void Application::ReturnToMenu(){m_state=StateMainMenu;}
void Application::OpenFileSelector(){if(m_config.showFileSelector)m_state=StateFileSelector;}
void Application::OpenSettings(){if(m_config.showSettings)m_state=StateSettings;}
bool Application::IsRunning()const{return m_state!=StateExit;}
void Application::RunMainMenu(){
 Elements el;EventDialog d(m_config.title?m_config.title:"Application");int row=0;
 if(m_config.showStart&&m_config.onStart)Button(el,d,row++,m_config.startLabel?m_config.startLabel:"Start",E_START);
 if(m_config.showFileSelector)Button(el,d,row++,m_config.filesLabel?m_config.filesLabel:"Files",E_FILES);
 if(m_config.showSettings&&(m_config.settingCount>0||m_config.onSettings))Button(el,d,row++,m_config.settingsLabel?m_config.settingsLabel:"Settings",E_SETTINGS);
 if(m_config.showQuit)Button(el,d,row++,m_config.quitLabel?m_config.quitLabel:"Quit",E_QUIT);
 if(!row)Button(el,d,row++,"Quit",E_QUIT);
 d.ShowDialog();int event=d.EventID();if(event==E_START&&m_config.onStart)m_config.onStart(*this);else if(event==E_FILES)OpenFileSelector();else if(event==E_SETTINGS)OpenSettings();else if(event==E_QUIT)Close();
}
void Application::RunFileSelector(){
 Entry*entries=(Entry*)malloc(sizeof(Entry)*MAX_ENTRIES);if(!entries){ReturnToMenu();return;}int count=0;Scan(m_currentDirectory,m_config,entries,&count);int page=0;bool done=false;
 while(!done&&m_state==StateFileSelector){Elements el;EventDialog d("Select file");PathLabel(el,d,m_currentDirectory);int pages=(count+ROWS-1)/ROWS;if(page>=pages&&pages>0)page=pages-1;if(page<0)page=0;
  for(int row=0;row<ROWS;++row){int index=page*ROWS+row;if(index<count){char label[NAME_LEN+3];Copy(label,entries[index].name,sizeof(label));if(entries[index].dir)Append(label,"/",sizeof(label));Button(el,d,row+1,label,E_ROW+row);}else Button(el,d,row+1," ",E_ROW+row,false);}
  Bottom(el,d,8,38,"<",E_PREV);Bottom(el,d,50,38,">",E_NEXT);Bottom(el,d,92,56,"Up",E_UP);Bottom(el,d,152,60,"Back",E_BACK);
  d.ShowDialog();int event=d.EventID();if(event==E_BACK){ReturnToMenu();done=true;}else if(event==E_UP){Parent(m_currentDirectory);Scan(m_currentDirectory,m_config,entries,&count);page=0;}else if(event==E_PREV){if(page>0)--page;}else if(event==E_NEXT){if((page+1)*ROWS<count)++page;}else if(event>=E_ROW&&event<E_ROW+ROWS){int index=page*ROWS+event-E_ROW;if(index<count){if(entries[index].dir){char next[PATH_LEN];Join(m_currentDirectory,entries[index].name,next,sizeof(next));Copy(m_currentDirectory,next,sizeof(m_currentDirectory));Append(m_currentDirectory,"\\",sizeof(m_currentDirectory));Scan(m_currentDirectory,m_config,entries,&count);page=0;}else{char selected[PATH_LEN];Join(m_currentDirectory,entries[index].name,selected,sizeof(selected));if(m_config.onFileSelected)m_config.onFileSelected(*this,selected);if(m_state==StateFileSelector)ReturnToMenu();done=true;}}}
 }free(entries);
}
void Application::RunSettings(){
 if(m_config.onSettings&&(!m_config.settings||m_config.settingCount<=0)){m_config.onSettings(*this);if(m_state==StateSettings)ReturnToMenu();return;}
 if(m_state!=StateSettings)return;if(!m_config.settings||m_config.settingCount<=0){ReturnToMenu();return;}
 int page=0;bool done=false;while(!done&&m_state==StateSettings){Elements el;EventDialog d("Settings");int pages=(m_config.settingCount+ROWS-1)/ROWS;if(page>=pages&&pages>0)page=pages-1;if(page<0)page=0;
  for(int row=0;row<ROWS;++row){int index=page*ROWS+row;if(index<m_config.settingCount){Setting&s=m_config.settings[index];char label[96],value[32];Copy(label,s.name?s.name:"Setting",sizeof(label));Append(label,": ",sizeof(label));Value(s,value,sizeof(value));Append(label,value,sizeof(label));Button(el,d,row,label,E_ROW+row);}else Button(el,d,row," ",E_ROW+row,false);}
  Bottom(el,d,8,38,"<",E_PREV);Bottom(el,d,50,38,">",E_NEXT);Bottom(el,d,94,116,"Back",E_BACK);
  d.ShowDialog();int event=d.EventID();if(event==E_BACK){ReturnToMenu();done=true;}else if(event==E_PREV){if(page>0)--page;}else if(event==E_NEXT){if((page+1)*ROWS<m_config.settingCount)++page;}else if(event>=E_ROW&&event<E_ROW+ROWS){int index=page*ROWS+event-E_ROW;if(index<m_config.settingCount)Adjust(m_config.settings[index],*this);if(m_state!=StateSettings)done=true;}
 }
}
}
