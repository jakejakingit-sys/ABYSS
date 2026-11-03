#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "abyss.h"

/* ABYSS: compact side-scrolling rune adventure. Map art is indexed ASCII;
 * runtime holds only one 660-byte room plus fixed-size player/save state. */
enum { TITLE, LOBBY, SELECT_LEVEL, UPGRADES, COLLECTION, STATS, PLAYING, PAUSE, ENDING };
enum { SOLID=1, HAZARD=2, COIN=3, RUNE=4, CHEST=5, EXIT=6, ENEMY=7, LADDER=8 };
typedef struct {
    char magic[4]; uint8_t version, unlocked, armor, hearts, energy;
    uint8_t abilities; uint32_t completed, treasure, secrets, coins, deaths, guardians, coinFlags[3];
    uint32_t checksum;
} SaveData;
typedef struct { int16_t x,y,vx,vy; uint8_t grounded,face,anim,animtick,invuln,energy,hp,regenLock,coyote,jumpBuffer; } Player;
static SaveData saveData;
static Player hero;
static char map[MAP_H][MAP_W];
static uint8_t scene=TITLE, selected=0, menu=0, upgrade=0, bossRunes=0, flash=0;
static uint8_t prevKeys[8];
static int16_t cameraX=0, enemyX=310, enemyDir=1;
static uint16_t frame=0, levelCoins=0;
static void before_gc(void) { gfx_End(); }
static void after_gc(void) { gfx_Begin(); gfx_SetDrawBuffer(); }
static uint8_t bit_count(uint32_t value) { uint8_t n=0; while(value){n+=(uint8_t)(value&1);value>>=1;} return n; }

static uint32_t checksum(const SaveData *s) {
    const uint8_t *p=(const uint8_t*)s; uint32_t h=2166136261u;
    for (uint16_t i=0;i<sizeof(SaveData)-4;i++) { h^=p[i]; h*=16777619u; }
    return h;
}
static void defaults(void) {
    memset(&saveData,0,sizeof(saveData)); memcpy(saveData.magic,"ABYS",4);
    saveData.version=2; saveData.unlocked=1;
}
static void save_game(void) {
    saveData.checksum=checksum(&saveData);
    uint8_t h=ti_Open("ABYSSSV","w");
    if (!h) return;
    ti_Write(&saveData,sizeof(saveData),1,h);
    ti_SetArchiveStatus(true,h); ti_Close(h);
}
static void load_game(void) {
    uint8_t h=ti_Open("ABYSSSV","r");
    if (!h) { defaults(); save_game(); return; }
    SaveData candidate; memset(&candidate,0,sizeof(candidate));
    size_t n=ti_Read(&candidate,sizeof(candidate),1,h); ti_Close(h);
    if (n!=1 || memcmp(candidate.magic,"ABYS",4) || candidate.version!=2 || candidate.checksum!=checksum(&candidate) || candidate.unlocked<1 || candidate.unlocked>20 || candidate.armor>3 || candidate.hearts>3 || candidate.energy>3) {
        defaults(); save_game(); return;
    }
    saveData=candidate;
}
static void keys_now(uint8_t k[8]) { kb_Scan(); for(uint8_t i=0;i<8;i++) k[i]=kb_Data[i]; }
static bool down(uint8_t *k,uint8_t group,uint8_t mask) { return (k[group]&mask)!=0; }
static bool edge(uint8_t *k,uint8_t group,uint8_t mask) { return down(k,group,mask) && !(prevKeys[group]&mask); }
static void commit_keys(uint8_t *k) { memcpy(prevKeys,k,8); }
static bool solid_at(int px,int py) {
    int tx=px/TILE,ty=py/TILE;
    if (tx<0||tx>=MAP_W||ty<0||ty>=MAP_H) return ty>=MAP_H;
    char c=map[ty][tx]; return c=='#'||c=='=';
}
static void start_level(uint8_t idx) {
    if (idx>=LEVEL_COUNT) idx=0;
    selected=idx;
    uint8_t coinOrdinal=0;
    for(uint8_t y=0;y<MAP_H;y++) { memcpy(map[y],abyss_levels[idx].rows[y],MAP_W); for(uint8_t x=0;x<MAP_W;x++)if(map[y][x]=='c'){uint8_t bitIndex=(uint8_t)(idx*4+coinOrdinal++);if(saveData.coinFlags[bitIndex>>5]&((uint32_t)1<<(bitIndex&31)))map[y][x]='.';} }
    hero.x=(int16_t)(abyss_levels[idx].spawn_x*TILE); hero.y=(int16_t)(abyss_levels[idx].spawn_y*TILE);
    hero.vx=hero.vy=0; hero.grounded=0; hero.face=1; hero.energy=(uint8_t)(30+saveData.energy*10);
    hero.hp=(uint8_t)(100+saveData.hearts*25); hero.regenLock=0;
    cameraX=0; enemyX=365; enemyDir=-1; bossRunes=0; levelCoins=0; scene=PLAYING;
}
static uint16_t armor_cost(void){return (uint16_t)(24+saveData.armor*28);}
static uint16_t heart_cost(void){return (uint16_t)(30+saveData.hearts*35);}
static uint16_t energy_cost(void){return (uint16_t)(22+saveData.energy*30);}
static void grant_level_rewards(void) {
    uint32_t bit=1UL<<selected;
    if (!(saveData.completed&bit)) { saveData.completed|=bit; saveData.coins+=8+(selected%5)*3; }
    if (selected%5==4 && !(saveData.guardians&(1UL<<(selected/5)))) saveData.guardians|=1UL<<(selected/5);
    if (saveData.unlocked<20 && saveData.unlocked<=selected+1) saveData.unlocked=selected+2;
    if (selected==2) saveData.abilities|=1; /* Heavy Landing */
    if (selected==5) saveData.abilities|=2; /* Phase Dash */
    if (selected==8) saveData.abilities|=4; /* Parry */
    if (selected==11) saveData.abilities|=8; /* Ground Wave */
    if (selected==15) saveData.abilities|=16; /* Whirlwind */
    if (selected==19) saveData.abilities|=32; /* Resonance */
    if (selected==19) { save_game(); scene=ENDING; }
    else { save_game(); scene=LOBBY; }
}
static void collect_tile(int tx,int ty) {
    char *c=&map[ty][tx];
    if (*c=='c') { uint8_t ordinal=0;for(int y=0;y<=ty;y++)for(int x=0;x<MAP_W;x++){if(y==ty&&x==tx)break;if(abyss_levels[selected].rows[y][x]=='c')ordinal++;}uint8_t bitIndex=(uint8_t)(selected*4+ordinal);uint32_t mask=(uint32_t)1<<(bitIndex&31);if(!(saveData.coinFlags[bitIndex>>5]&mask)){saveData.coinFlags[bitIndex>>5]|=mask;saveData.coins+=3;levelCoins+=3;save_game();}*c='.'; }
    else if (*c=='k') { *c='.'; if(!(saveData.treasure&(1UL<<selected))) { saveData.treasure|=1UL<<selected; saveData.coins+=15; levelCoins+=15; } }
}
static void activate_pulse(void) {
    uint8_t tx=(uint8_t)((hero.x+8)/TILE), ty=(uint8_t)((hero.y+8)/TILE);
    bool found=false;
    for(int dy=-2;dy<=2;dy++) for(int dx=-3;dx<=3;dx++) {
        int x=tx+dx,y=ty+dy; if(x<0||x>=MAP_W||y<0||y>=MAP_H) continue;
        if(map[y][x]=='r') { map[y][x]='.'; found=true; flash=10; }
    }
    if (selected%5==4 && found && bossRunes<3) { bossRunes++; flash=14; }
    if (found && selected%5!=4) { saveData.secrets|=1UL<<selected; }
    if (found) save_game();
}
static void update_game(uint8_t *k) {
    int oldx=hero.x,oldy=hero.y;
    bool left=down(k,7,kb_Left), right=down(k,7,kb_Right);
    if(left){hero.vx-=2;hero.face=0;} if(right){hero.vx+=2;hero.face=1;}
    if(!left&&!right) { if(hero.vx>0)hero.vx--; else if(hero.vx<0)hero.vx++; }
    if(hero.vx>34)hero.vx=34; if(hero.vx< -34)hero.vx=-34;
    if(edge(k,1,kb_2nd))hero.jumpBuffer=6;else if(hero.jumpBuffer)hero.jumpBuffer--;
    if(hero.grounded)hero.coyote=6;else if(hero.coyote)hero.coyote--;
    if(hero.jumpBuffer&&hero.coyote){hero.vy=-59;hero.grounded=0;hero.coyote=0;hero.jumpBuffer=0;hero.anim=2;hero.animtick=0;}
    if(!down(k,1,kb_2nd)&&hero.vy< -25)hero.vy=-25;
    if(edge(k,2,kb_Alpha) && hero.energy>=2) { hero.energy-=2; activate_pulse(); hero.anim=8; }
    if(edge(k,1,kb_Mode) && hero.energy>=3) {
        hero.energy-=3;
        if(saveData.abilities&32) { for(uint8_t y=0;y<MAP_H;y++)for(uint8_t x=0;x<MAP_W;x++)if(map[y][x]=='r')map[y][x]='.';bossRunes=3;flash=18;hero.anim=9;save_game(); }
        else if(saveData.abilities&16) { enemyX=hero.x+(hero.face?150:-150);flash=10;hero.anim=8; }
        else if(saveData.abilities&8) { for(int y=11;y<MAP_H;y++)for(int x=(hero.x/TILE)-8;x<(hero.x/TILE)+8;x++)if(x>=0&&x<MAP_W&&map[y][x]=='r')map[y][x]='.';if(hero.grounded)hero.vy=16;flash=10; }
        else if(saveData.abilities&4) { hero.invuln=50; hero.anim=9; flash=8; }
        else if(saveData.abilities&2) { hero.vx=hero.face?70:-70; hero.anim=7; flash=8; }
        else if(saveData.abilities&1) { if(hero.grounded){hero.vy=22;hero.grounded=0;}flash=6; }
        else { hero.invuln=12; flash=4; }
    }
    if(down(k,7,kb_Up)) {
        int tx=(hero.x+8)/TILE,ty=(hero.y+16)/TILE;
        if(tx>=0&&tx<MAP_W&&ty>=0&&ty<MAP_H&&map[ty][tx]=='l') hero.vy=-20;
    }
    hero.vy+=3; if(hero.vy>48)hero.vy=48;
    hero.x+=hero.vx/4;
    if(hero.x<0)hero.x=0; if(hero.x>MAP_W*TILE-16)hero.x=MAP_W*TILE-16;
    if(solid_at(hero.x+2,hero.y+4)||solid_at(hero.x+14,hero.y+4)||solid_at(hero.x+2,hero.y+14)||solid_at(hero.x+14,hero.y+14)) hero.x=oldx;
    hero.y+=hero.vy/4; hero.grounded=0;
    if(hero.vy>=0 && (solid_at(hero.x+3,hero.y+18)||solid_at(hero.x+13,hero.y+18))) {hero.y=(int16_t)(((hero.y+18)/TILE)*TILE-18);hero.vy=0;hero.grounded=1;hero.coyote=6;}
    if(hero.vy<0 && (solid_at(hero.x+3,hero.y+2)||solid_at(hero.x+13,hero.y+2))) {hero.y=oldy;hero.vy=0;}
    if(hero.y>224){hero.y=oldy;hero.vy=0;}
    for(int y=hero.y/TILE;y<=(hero.y+17)/TILE;y++) for(int x=hero.x/TILE;x<=(hero.x+15)/TILE;x++) if(x>=0&&x<MAP_W&&y>=0&&y<MAP_H) {
        char c=map[y][x]; if(c=='c'||c=='k')collect_tile(x,y);
        if(c=='^' && !hero.invuln) { uint8_t damage=(uint8_t)(12-(saveData.armor*2)); hero.hp=hero.hp>damage?hero.hp-damage:0;hero.regenLock=120;hero.invuln=40;hero.vy=-27;hero.vx=hero.face?-20:20;if(hero.energy)hero.energy--;if(!hero.hp){saveData.deaths++;save_game();start_level(selected);return;} }
        if(c=='d' && (selected%5!=4 || bossRunes>=3)) { grant_level_rewards(); return; }
    }
    if(selected%5==4) {
        enemyX+=enemyDir*2; if(enemyX<260||enemyX>520)enemyDir=-enemyDir;
        if(abs(hero.x-enemyX)<20 && !hero.invuln){uint8_t damage=(uint8_t)(15-saveData.armor*2);hero.hp=hero.hp>damage?hero.hp-damage:0;hero.regenLock=120;hero.invuln=45;hero.vx=hero.x<enemyX?-35:35;hero.vy=-24;if(!hero.hp){saveData.deaths++;save_game();start_level(selected);return;}}
    } else {
        enemyX+=enemyDir; if(enemyX<230||enemyX>470)enemyDir=-enemyDir;
        if(abs(hero.x-enemyX)<16 && !hero.invuln){uint8_t damage=(uint8_t)(10-saveData.armor*2);hero.hp=hero.hp>damage?hero.hp-damage:0;hero.regenLock=120;hero.invuln=36;hero.vx=hero.x<enemyX?-24:24;hero.vy=-22;if(!hero.hp){saveData.deaths++;save_game();start_level(selected);return;}}
    }
    if(hero.invuln)hero.invuln--;
    if(hero.regenLock)hero.regenLock--; else if(frame%90==0&&hero.hp<(100+saveData.hearts*25))hero.hp++;
    if(frame%24==0&&hero.energy<(30+saveData.energy*10))hero.energy++;
    if(hero.grounded) { hero.anim=(hero.vx==0)?0:1; } else if(hero.vy<0)hero.anim=2; else hero.anim=3;
    hero.animtick++; if(hero.animtick>8)hero.animtick=0;
    if(flash)flash--;
    if(hero.x-cameraX>220)cameraX=hero.x-220; if(hero.x-cameraX<68)cameraX=hero.x-68;
    if(cameraX<0)cameraX=0; if(cameraX>MAP_W*TILE-320)cameraX=MAP_W*TILE-320;
    (void)oldy;
}
static uint8_t ink(uint8_t world,uint8_t what) {
    return what? (uint8_t)(19+(what>5?5:what)):0;
}
static void set_world_palette(uint8_t world) {
    static const uint8_t rgb[4][5][3]={
        {{31,43,62},{57,76,103},{116,129,149},{112,93,139},{188,225,173}},
        {{24,66,74},{45,101,105},{88,154,151},{145,112,76},{112,230,225}},
        {{45,23,31},{105,37,40},{171,65,32},{226,117,28},{255,211,93}},
        {{21,15,44},{60,35,100},{105,64,166},{91,163,232},{225,210,255}}
    };
    for(uint8_t i=0;i<5;i++) gfx_palette[20+i]=gfx_RGBTo1555(rgb[world&3][i][0],rgb[world&3][i][1],rgb[world&3][i][2]);
}
static void tile_draw(int x,int y,char c,uint8_t w) {
    if(c=='#') { gfx_SetColor(ink(w,1)); gfx_FillRect(x,y,16,16); gfx_SetColor(ink(w,2)); gfx_HorizLine(x,y+2,16); gfx_HorizLine(x+3,y+8,6); gfx_HorizLine(x+1,y+14,10); gfx_SetColor(ink(w,3)); gfx_VertLine(x+13,y+4,5); }
    else if(c=='=') { gfx_SetColor(ink(w,3)); gfx_FillRect(x,y+5,16,8); gfx_SetColor(ink(w,4)); gfx_HorizLine(x,y+5,16); gfx_SetColor(ink(w,2)); gfx_VertLine(x+3,y+9,4); gfx_VertLine(x+12,y+9,4); }
    else if(c=='^') { gfx_SetColor(ink(w,5)); gfx_FillTriangle(x,y+15,x+8,y+1,x+15,y+15); gfx_SetColor(ink(w,2)); gfx_HorizLine(x,y+15,16); }
    else if(c=='c') { gfx_SetColor(ink(w,5)); gfx_FillCircle(x+8,y+8,5); gfx_SetColor(ink(w,3)); gfx_FillCircle(x+8,y+8,2); }
    else if(c=='k') { gfx_SetColor(ink(w,3)); gfx_FillRect(x+2,y+5,12,10); gfx_SetColor(ink(w,5)); gfx_HorizLine(x+2,y+5,12); gfx_VertLine(x+7,y+6,9); gfx_SetColor(ink(w,4)); gfx_FillRect(x+1,y+2,14,4); }
    else if(c=='r') { gfx_SetColor(ink(w,5)); gfx_FillTriangle(x+8,y+1,x+14,y+8,x+8,y+15); gfx_FillTriangle(x+8,y+1,x+2,y+8,x+8,y+15); }
}
static void hero_draw(int sx,int sy) {
    /* 16x18 hand-drawn pixel adventurer; eye, hood and scarf remain readable
       while running, hopping, phasing and channelling a rune. */
    static const char *p[18]={"......DDDD......","....DDHHHHDD....","...DHHHHHHHHD...","...DHHSSHHHHD...","...DHHHHHHHHD...","....DHRRRHD.....","......RRR........",".....BRRRB.......","....BBBWWBBB.....","....BWWWWWWB.....","....BWWWWWWB.....",".....BWWWWB......","......BBBW.......","......LL.LL......",".....LL...LL.....","....LL.....LL....","...DD.......DD...","................"};
    int bob=(hero.grounded&&hero.vx&&((frame>>2)&1))?1:0;
    if(hero.anim==2)bob=-1; if(hero.anim==7) { sx+=hero.face?-3:3; }
    if(hero.anim==8) { gfx_SetColor(ink(abyss_levels[selected].world,5)); gfx_FillCircle(sx+8,sy+9,13); }
    for(int y=0;y<18;y++) for(int x=0;x<16;x++) {
        char q=p[y][x]; uint8_t col=0;
        if(q=='D')col=ink(abyss_levels[selected].world,2); else if(q=='H')col=ink(abyss_levels[selected].world,3);
        else if(q=='S')col=ink(abyss_levels[selected].world,5); else if(q=='R')col=ink(abyss_levels[selected].world,4);
        else if(q=='B')col=34; else if(q=='W')col=235; else if(q=='L')col=34;
        if(col){gfx_SetColor(col);int dx=hero.face?x:15-x;gfx_FillRect(sx+dx,sy+bob+y,1,1);}
    }
}
static void number(int x,int y,uint32_t n,uint8_t digits) { gfx_SetTextXY(x,y);gfx_PrintUInt(n,digits); }
static void hud(void) {
    uint8_t maxHp=(uint8_t)(100+saveData.hearts*25), maxEnergy=(uint8_t)(30+saveData.energy*10);
    gfx_SetColor(0);gfx_FillRect(0,0,320,22);gfx_SetColor(ink(abyss_levels[selected].world,5));gfx_FillRect(8,7,104,8);
    gfx_SetColor(ink(abyss_levels[selected].world,2));gfx_FillRect(10,9,100,4);
    gfx_SetColor(ink(abyss_levels[selected].world,4));gfx_FillRect(10,9,(uint8_t)(hero.hp*100/maxHp),4);
    gfx_SetColor(ink(abyss_levels[selected].world,5));gfx_FillRect(8,17,104,3);gfx_SetColor(ink(abyss_levels[selected].world,3));gfx_FillRect(10,18,(uint8_t)(hero.energy*100/maxEnergy),1);
    gfx_SetTextFGColor(255);gfx_SetTextBGColor(0);gfx_SetTextXY(120,5);gfx_PrintString(abyss_levels[selected].name);
    gfx_SetTextXY(258,5);gfx_PrintString("C");number(268,5,saveData.coins,4);
    if(selected%5==4){gfx_SetTextXY(8,24);gfx_PrintString("RUNE SEALS");for(uint8_t i=0;i<3;i++){gfx_SetColor(i<bossRunes?ink(abyss_levels[selected].world,5):ink(abyss_levels[selected].world,1));gfx_FillCircle(92+i*14,29,4);}}
}
static void draw_world(void) {
    uint8_t w=abyss_levels[selected].world;
    set_world_palette(w);
    gfx_SetColor(ink(w,0));gfx_FillScreen(ink(w,0));
    /* Deep parallax masonry and distant spires. */
    for(int i=0;i<18;i++){int x=(i*47-(cameraX/3)%47);gfx_SetColor(ink(w,(i%3)+1));gfx_VertLine(x,35+(i%4)*11,135);gfx_HorizLine(x,67+(i%5)*15,20);}
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) {
        char c=map[y][x];int sx=x*TILE-cameraX,sy=y*TILE;
        if(sx<-16||sx>320)continue;
        if(c=='#'||c=='='||c=='^'||c=='c'||c=='k'||c=='r')tile_draw(sx,sy,c,w);
        else if(c=='d') {gfx_SetColor(ink(w,4));gfx_FillRect(sx+2,sy,12,16);gfx_SetColor(ink(w,2));gfx_FillRect(sx+5,sy+3,6,13);gfx_SetColor(ink(w,5));gfx_FillCircle(sx+12,sy+8,1);}
    }
    /* Patrolling crawler creature. */
    int ex=enemyX-cameraX; if(ex>-20&&ex<330){gfx_SetColor(ink(w,3));gfx_FillEllipse(ex+8,206,12,7);gfx_SetColor(ink(w,5));gfx_FillCircle(ex+12,204,2);gfx_SetColor(ink(w,2));for(int i=0;i<3;i++)gfx_Line(ex+2+i*6,208,ex-2+i*6,214);}
    if(selected%5==4) { /* guardian: four unique themed silhouette treatments */
        int bx=enemyX-cameraX+5,by=143;gfx_SetColor(ink(w,2));gfx_FillEllipse(bx,by,35,42);
        gfx_SetColor(ink(w,3));gfx_FillEllipse(bx,by-10,26,22);
        gfx_SetColor(ink(w,5));gfx_FillCircle(bx-9,by-12,4);gfx_FillCircle(bx+9,by-12,4);
        gfx_SetColor(ink(w,4));gfx_FillTriangle(bx-25,by-28,bx-19,by-45,bx-11,by-27);gfx_FillTriangle(bx+25,by-28,bx+19,by-45,bx+11,by-27);
        if(w==1){gfx_SetColor(ink(w,5));gfx_FillCircle(bx,by+8,14);gfx_SetColor(ink(w,3));for(int i=0;i<5;i++)gfx_Line(bx-12+i*6,by+2,bx-17+i*9,by+20);}
        if(w==2){gfx_SetColor(ink(w,5));gfx_FillEllipse(bx,by+25,30,6);}
        if(w==3){gfx_SetColor(ink(w,4));gfx_FillCircle(bx,by,46);gfx_SetColor(ink(w,0));gfx_FillCircle(bx,by,36);gfx_SetColor(ink(w,3));gfx_FillEllipse(bx,by,24,35);}
    }
    if(flash){gfx_SetColor(ink(w,5));gfx_Circle(hero.x-cameraX+8,hero.y+9,13+flash%5);}
    if(!hero.invuln||!(frame&2))hero_draw(hero.x-cameraX,hero.y);
    hud();
}
static void panel(const char *title) { gfx_SetColor(0);gfx_FillScreen(0);gfx_SetColor(ink(0,2));for(int i=0;i<13;i++)gfx_HorizLine(0,15+i*18,320);gfx_SetColor(ink(0,4));gfx_FillRect(16,20,288,32);gfx_SetTextFGColor(255);gfx_SetTextBGColor(ink(0,4));gfx_SetTextXY(25,29);gfx_PrintString(title);gfx_SetTextBGColor(0); }
static void title_draw(void){panel("ABYSS");gfx_SetTextXY(26,73);gfx_PrintString("THE DEEP REMEMBERS YOUR NAME");gfx_SetTextXY(26,111);gfx_PrintString("2ND  BEGIN DESCENT");gfx_SetTextXY(26,138);gfx_PrintString("ARROWS MOVE     ALPHA RUNE PULSE");gfx_SetTextXY(26,165);gfx_PrintString("MODE ABILITY    CLEAR PAUSE");gfx_SetColor(ink(3,4));gfx_FillCircle(260,128,25);gfx_SetColor(ink(3,2));gfx_FillCircle(260,128,18);gfx_SetColor(ink(3,5));gfx_FillTriangle(260,105,244,130,260,151);gfx_FillTriangle(260,105,276,130,260,151);}
static void lobby_draw(void){panel("THE WAYSTATION");static const char *opts[4]={"PLAY","UPGRADES","COLLECTION","STATS"};for(int i=0;i<4;i++){gfx_SetTextFGColor(i==menu?ink(0,5):255);gfx_SetTextXY(42,78+i*27);gfx_PrintString(i==menu?"> ":"  ");gfx_PrintString(opts[i]);}gfx_SetTextFGColor(255);gfx_SetTextXY(188,80);gfx_PrintString("A quiet place between worlds.");gfx_SetTextXY(188,109);gfx_PrintString("Coins");number(240,109,saveData.coins,4);gfx_SetTextXY(188,139);gfx_PrintString("Depth");number(240,139,saveData.unlocked,2);gfx_SetTextXY(24,207);gfx_PrintString("UP/DOWN SELECT     ENTER CONFIRM");}
static void select_draw(void){static const char *worlds[4]={"FORGOTTEN CRYPT","SUNKEN MINES","INFERNAL DEPTHS","THE VOID"};panel("CHOOSE A DESCENT");uint8_t first=(selected/5)*5;gfx_SetTextFGColor(ink(first/5,5));gfx_SetTextXY(35,62);gfx_PrintString(worlds[first/5]);for(uint8_t i=0;i<5;i++){uint8_t n=first+i;int y=82+i*23;bool done=(saveData.completed&(1UL<<n))!=0;gfx_SetTextFGColor(n<saveData.unlocked? (done?ink(first/5,5):255):ink(first/5,2));gfx_SetTextXY(39,y);gfx_PrintString(n==selected?"> ":"  ");number(58,y,n+1,2);gfx_PrintString(" ");gfx_PrintString(abyss_levels[n].name);if(n%5==4){gfx_SetTextFGColor(ink(first/5,4));gfx_PrintString("  GUARDIAN");}}gfx_SetTextFGColor(255);gfx_SetTextXY(22,207);gfx_PrintString("LEFT/RIGHT WORLD   UP/DOWN LEVEL   ENTER");}
static void upgrades_draw(void){panel("WAYSTATION WORKSHOP");static const char *names[3]={"ARMOR","MAXIMUM HEALTH","ENERGY CAPACITY"};uint8_t lv[3]={saveData.armor,saveData.hearts,saveData.energy};uint16_t cost[3]={armor_cost(),heart_cost(),energy_cost()};for(int i=0;i<3;i++){gfx_SetTextFGColor(i==upgrade?ink(0,5):255);gfx_SetTextXY(36,78+i*34);gfx_PrintString(i==upgrade?"> ":"  ");gfx_PrintString(names[i]);gfx_SetTextXY(219,78+i*34);number(219,78+i*34,lv[i],1);gfx_PrintString(" / 3");gfx_SetTextXY(36,91+i*34);gfx_SetTextFGColor(ink(0,4));if(lv[i]<3){gfx_PrintString("COST ");number(80,91+i*34,cost[i],3);}else gfx_PrintString("MAXIMUM");}gfx_SetTextFGColor(255);gfx_SetTextXY(32,203);gfx_PrintString("COINS");number(78,203,saveData.coins,4);gfx_PrintString("     ENTER BUY   CLEAR BACK");}
static void collection_draw(void){uint8_t percent=(uint8_t)((bit_count(saveData.completed)*70+bit_count(saveData.treasure)*15+bit_count(saveData.secrets)*15)/20);panel("RELICS & DISCOVERIES");gfx_SetTextFGColor(255);gfx_SetTextXY(30,71);gfx_PrintString("TREASURE ROOMS");number(180,71,bit_count(saveData.treasure),2);gfx_PrintString(" / 20");gfx_SetTextXY(30,99);gfx_PrintString("RUNE SECRETS");number(180,99,bit_count(saveData.secrets),2);gfx_PrintString(" / 20");gfx_SetTextXY(30,127);gfx_PrintString("COMPLETION");number(180,127,percent,3);gfx_PrintString(" %");gfx_SetTextXY(30,158);gfx_PrintString("GUARDIAN RELICS");for(uint8_t i=0;i<4;i++){gfx_SetColor(saveData.guardians&(1UL<<i)?ink(i,5):ink(0,2));gfx_FillCircle(72+i*52,188,12);if(saveData.guardians&(1UL<<i)){gfx_SetColor(ink(i,4));gfx_Circle(72+i*52,188,8);}}gfx_SetTextXY(30,213);gfx_PrintString("CLEAR BACK");}
static void stats_draw(void){panel("DESCENT RECORD");gfx_SetTextFGColor(255);gfx_SetTextXY(30,68);gfx_PrintString("LEVELS CLEARED");number(211,68,bit_count(saveData.completed),2);gfx_SetTextXY(30,96);gfx_PrintString("GUARDIANS SOLVED");number(211,96,bit_count(saveData.guardians),1);gfx_SetTextXY(30,124);gfx_PrintString("TREASURE ROOMS");number(211,124,bit_count(saveData.treasure),2);gfx_SetTextXY(30,152);gfx_PrintString("RUNE SECRETS");number(211,152,bit_count(saveData.secrets),2);gfx_SetTextXY(30,180);gfx_PrintString("FALLS RECORDED");number(211,180,saveData.deaths,4);gfx_SetTextXY(30,210);gfx_PrintString("CLEAR BACK");}
static void pause_draw(void){panel("DESCENT PAUSED");gfx_SetTextFGColor(255);gfx_SetTextXY(35,91);gfx_PrintString("ENTER  RESUME");gfx_SetTextXY(35,121);gfx_PrintString("CLEAR  WAYSTATION");gfx_SetTextXY(35,169);gfx_PrintString("The dark will wait.");}
static void ending_draw(void){panel("THE ABYSS REMEMBERS");gfx_SetTextFGColor(255);gfx_SetTextXY(24,76);gfx_PrintString("The last seal opens. The dark lets go.");gfx_SetTextXY(24,108);gfx_PrintString("Beyond the deep, a small dawn begins.");gfx_SetTextXY(24,153);gfx_PrintString("Every lost name returns to the stars.");gfx_SetTextXY(24,206);gfx_PrintString("ENTER  WAYSTATION");}
int main(void) {
    uint8_t k[8]; load_game(); kb_SetMode(MODE_3_CONTINUOUS); gfx_Begin(); gfx_SetDrawBuffer(); ti_SetGCBehavior(before_gc,after_gc);
    while(1) {
        keys_now(k); frame++;
        if(scene==TITLE) {if(edge(k,1,kb_2nd)||edge(k,6,kb_Enter)){scene=LOBBY;menu=0;}}
        else if(scene==LOBBY){if(edge(k,7,kb_Up)&&menu)menu--;if(edge(k,7,kb_Down)&&menu<3)menu++;if(edge(k,6,kb_Enter)){if(menu==0){scene=SELECT_LEVEL;selected=0;}else if(menu==1){scene=UPGRADES;upgrade=0;}else if(menu==2)scene=COLLECTION;else scene=STATS;}}
        else if(scene==SELECT_LEVEL){if(edge(k,7,kb_Left)&&selected>=5)selected-=5;if(edge(k,7,kb_Right)&&selected<15)selected+=5;if(edge(k,7,kb_Up)&&selected%5)selected--;if(edge(k,7,kb_Down)&&selected%5<4&&selected<19)selected++;if(edge(k,6,kb_Enter)&&selected<saveData.unlocked)start_level(selected);if(edge(k,6,kb_Clear))scene=LOBBY;}
        else if(scene==UPGRADES){if(edge(k,7,kb_Up)&&upgrade)upgrade--;if(edge(k,7,kb_Down)&&upgrade<2)upgrade++;if(edge(k,6,kb_Enter)){uint16_t cost=upgrade==0?armor_cost():upgrade==1?heart_cost():energy_cost();uint8_t *lv=upgrade==0?&saveData.armor:upgrade==1?&saveData.hearts:&saveData.energy;if(*lv<3&&saveData.coins>=cost){saveData.coins-=cost;(*lv)++;save_game();}}if(edge(k,6,kb_Clear))scene=LOBBY;}
        else if(scene==COLLECTION||scene==STATS){if(edge(k,6,kb_Clear)||edge(k,6,kb_Enter))scene=LOBBY;}
        else if(scene==PLAYING){if(edge(k,6,kb_Clear))scene=PAUSE;else update_game(k);}
        else if(scene==PAUSE){if(edge(k,6,kb_Enter))scene=PLAYING;if(edge(k,6,kb_Clear))scene=LOBBY;}
        else if(scene==ENDING){if(edge(k,6,kb_Enter)||edge(k,6,kb_Clear))scene=LOBBY;}
        set_world_palette((uint8_t)(selected/5));
        gfx_SetDrawBuffer();
        if(scene==TITLE)title_draw();else if(scene==LOBBY)lobby_draw();else if(scene==SELECT_LEVEL)select_draw();else if(scene==UPGRADES)upgrades_draw();else if(scene==COLLECTION)collection_draw();else if(scene==STATS)stats_draw();else if(scene==PLAYING)draw_world();else if(scene==PAUSE)pause_draw();else ending_draw();
        gfx_SwapDraw(); gfx_Wait(); commit_keys(k);
    }
    gfx_End(); return 0;
}
