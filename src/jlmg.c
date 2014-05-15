#define ALLEGRO_STATICLINK
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <strings.h>
#include <pthread.h>

#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>

#define PIXELS_PER_METER 5
#define NO_LASER "No LASER left :("

#define MAX(a, b) ((a > b) ? a : b)
#define MIN(a, b) ((a > b) ? b : a)
#define ABS(a) ((a > 0) ? a : (-a))

typedef struct ActorStruct* Actor;

struct ActorStruct {
  float x, y;
  float vx, vy;
  float ax, ay;
  float rot;
  int hFlip, vFlip;
  unsigned char graphic;
  int isStatic;
  int physics;
  int collision;
  int fresh;
  int hidden;
  void (*tick)(Actor, float);
  void (*created)(Actor);
  void (*destroyed)(Actor);
  void (*render)(Actor, float);
  Actor owner;
}; 

Actor* actors = NULL;
int actorsLength = 0;
int actorsCapacity = 0;
int wonGame = 0;

Actor player;
float running = 0;
ALLEGRO_DISPLAY* display;
ALLEGRO_EVENT_QUEUE* queue;
ALLEGRO_BITMAP* spritesheet;
ALLEGRO_BITMAP** graphics;
ALLEGRO_SAMPLE* stepSound;
ALLEGRO_SAMPLE* laserSound;
ALLEGRO_SAMPLE* kbdSound;
ALLEGRO_SAMPLE* startSound;
ALLEGRO_SAMPLE* stopSound;
ALLEGRO_SAMPLE* bweepSound;
unsigned char level[4096] = {
#include "level.h"
};
int levelWidth, levelHeight;
int quit = 0;

float heliTime = 0;
int channeling = 0;
float emp;
int downing;
int lasering;
float battery = 10;
int lefting;
int ready = 0;

//function headers why the HELL are they here???

Actor makeActor(float, float, float, float, float, float, float, unsigned char, int, int, int, void (*tick)(Actor, float), void (*created)(Actor), void (*destroyed)(Actor), void (*render)(Actor, float), Actor);
void* _eventThread(void* param);
Actor addActor(Actor);
void destroyActor(Actor);
void drawActor(Actor, float);
void drawString(int, int, char*);
void drawSpeechBubble(Actor, char*);
void tickPlayer(Actor, float);
void destroyedPlayer(Actor);
void renderPlayer(Actor, float);
float tintMult(int);

Actor makeActor(float x, float y, float vx, float vy, float ax, float ay, float rot, unsigned char graphic, int isStatic, int physics, int collision, void (*tick)(Actor, float), void (*created)(Actor), void (*destroyed)(Actor), void (*render)(Actor, float), Actor owner) {
  Actor a;
  int i;
  if (actorsCapacity == 0) {
    actors = calloc(sizeof(Actor), 64);
    actorsCapacity = 64;
  } else if (actorsLength >= actorsCapacity) {
    Actor* newa = calloc(sizeof(Actor), actorsCapacity * 2);
    for (i = 0; i < actorsCapacity; i++) {
      newa[i] = actors[i];
    }
    actorsCapacity *= 2;
    free(actors);
    actors = newa;
  }

  a = malloc(sizeof(struct ActorStruct));
  a->x = x;
  a->y = y;
  a->vx = vx;
  a->vy = vy;
  a->ax = ax;
  a->ay = ay;
  a->rot = rot;
  a->hFlip = 0;
  a->vFlip = 0;
  a->graphic = graphic;
  a->isStatic = isStatic;
  a->physics = physics;
  a->collision = collision;
  a->fresh = 1;
  a->hidden = 0;
  a->tick = tick;
  a->created = created;
  a->destroyed = destroyed;
  a->render = render;

  for (i = 0; i < actorsCapacity; i++) {
    if (actors[i] == NULL) {
      actors[i] = a;
      break;
    }
  }

  actorsLength++;

  if (a->created != NULL) a->created(a);
  return a;
}

Actor addActor(Actor); //TODO FIXME NYI

void destroyActor(Actor a) { //TODO shrink sparse array if need be? nah
  if (a == NULL) return;
  int i;
  for (i = 0; i < actorsCapacity; i++) {
    if (actors[i] == a) {
      if (a->destroyed != NULL) a->destroyed(a);
      actors[i] = NULL;
      free(a);
      break;
    }
  }
}

float tintMult(int tile) {
  if (level[tile] < 15 || (level[tile] > 31 && level[tile] < 128) || level[tile] > 160) return 1;
  if (player == NULL) return 0;
  float m = 1, n = 1;
  m *= MAX(0, MIN(1, pow(player->x - (tile % levelWidth * 8) + 4, 2) / 2048.0));
  m = 1 - m;
  n *= MAX(0, MIN(1, pow(player->y - (tile / levelWidth * 8) + 4, 2) / 512.0));
  n = 1 - n;
  m *= n;
  m += (10.0 - heliTime) / 10.0;
  //printf("%f ", ABS(player->y - (tile / levelWidth) * 8 + 4) / 96);
  //printf("%f ", m);
  return MAX(0, MIN(1, m));
}

void drawActor(Actor a, float d) {
  int flags = 0;
  if (a == NULL || a->hidden) return;
  if (a->hFlip) flags |= ALLEGRO_FLIP_HORIZONTAL;
  if (a->vFlip) flags |= ALLEGRO_FLIP_VERTICAL;
  al_draw_rotated_bitmap(graphics[a->graphic], 4, 4, a->x, a->y, a->rot, flags);
  if (a->render != NULL) a->render(a, d);
}

void drawString(int x, int y, char* str) {
  if (str == NULL) return;
  int i;
  for (i = 0; str[i] != '\0'; i++) { 
    unsigned char c = (unsigned char)(str[i]);
    al_draw_bitmap(graphics[c], x + i * 8, y, 0);
  }
}
void drawSpeechBubble(Actor a, char* str) {
  if (a == NULL || str == NULL) return;
  int len = strlen(str) - 1;
  int i;
  al_draw_bitmap(graphics[198], a->x - 8 - len * 4, a->y - 32, 0);
  al_draw_bitmap(graphics[214], a->x - 8 - len * 4, a->y - 24, 0);
  al_draw_bitmap(graphics[230], a->x - 8 - len * 4, a->y - 16, 0);

  for (i = 0; i <= len; i++) {
    unsigned char c = (unsigned char)(str[i]);
    al_draw_bitmap(graphics[199], a->x - len * 4 + i * 8, a->y - 32, 0);
    al_draw_bitmap(graphics[c], a->x - len * 4 + i * 8, a->y - 24, 0);
    al_draw_bitmap(graphics[215], a->x - len * 4 + i * 8, a->y - 16, 0);
  }
  al_draw_bitmap(graphics[231], a->x + len * 4, a->y - 16, 0);

  al_draw_bitmap(graphics[200], a->x + 8 + len * 4, a->y - 32, 0);
  al_draw_bitmap(graphics[216], a->x + 8 + len * 4, a->y - 24, 0);
  al_draw_bitmap(graphics[232], a->x + 8 + len * 4, a->y - 16, 0);
}

void* _eventThread(void* param) {
  queue = al_create_event_queue();
  ALLEGRO_EVENT e;

  al_register_event_source(queue, al_get_display_event_source(display));
  al_register_event_source(queue, al_get_keyboard_event_source());

  while(!quit) {
    al_wait_for_event(queue, &e);
    if (e.type == ALLEGRO_EVENT_DISPLAY_CLOSE) quit = 1;
    else if (e.type == ALLEGRO_EVENT_KEY_DOWN) {
      switch (e.keyboard.keycode) {
        case ALLEGRO_KEY_ESCAPE:
          quit = 1;
          break;
        case ALLEGRO_KEY_UP:
          if (player != NULL && player->vx == 0) channeling = 1;
          else {
            channeling = 0;
            emp = 0;
          }
          break;
        case ALLEGRO_KEY_DOWN:
          downing = 1;
          break;
        case ALLEGRO_KEY_LEFT:
          if (player != NULL) {
            lefting = 1;
            player->vx = -10;
          }
          break;
        case ALLEGRO_KEY_RIGHT:
          if (player != NULL) {
            lefting = 0;
            player->vx += 10;
          }
          break;
        default:
          if (player != NULL) lasering = 1;
          else {
            //spawn player and send away helicopter
  if (!wonGame) player = makeActor(258, 112, 0, 0, 0, 0, 0, 160, 0, 1, 1, tickPlayer, NULL, destroyedPlayer, renderPlayer, NULL);
            battery = 10;
          }
          break;
      }
    } else if (e.type == ALLEGRO_EVENT_KEY_UP) {
      switch (e.keyboard.keycode) {
        case ALLEGRO_KEY_UP:
          channeling = 0;
          emp = 0;
          break;
        case ALLEGRO_KEY_DOWN:
          downing = 0;
          break;
        case ALLEGRO_KEY_LEFT:
          if (player != NULL && lefting) player->vx = 0;
          break;
        case ALLEGRO_KEY_RIGHT:
          if (player != NULL && !lefting) player->vx = 0;
          break;
        default:
          lasering = 0;
          break;
      }
    }
  }

  al_flush_event_queue(queue);
  al_destroy_event_queue(queue);
  pthread_exit(0);
}

void tickPlayer(Actor a, float d) {
  static float frames = 0;
  static int sound = 0;
  if (a->vx > 0) a->hFlip = 0;
  else if (a->vx < 0) a->hFlip = 1;
  if (a->vy != 0) a->vx = 0;
  static ALLEGRO_SAMPLE_ID lid;
  static int playan = 0;
  if (lasering && !playan && battery > 0) {
    al_play_sample(laserSound, 1, 0, 1, ALLEGRO_PLAYMODE_LOOP, &lid);
    al_play_sample(bweepSound, 1, 0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
    playan = 1;
  } else if (!lasering || battery <= 0){
    if (playan) al_stop_sample(&lid);
    playan = 0;
  }

  if (lasering && battery > 0) battery -= d;

  if (channeling) {
    a->graphic = 165;
    frames = 0;
    emp += d;
    if (emp > 1) {
      emp = 0;
      int l;
      for (l = 0; l < actorsCapacity; l++) {
        if (actors[l] == NULL) continue;
        if (actors[l]->graphic == 166 && ABS(actors[l]->x - a->x) < 32 &&
            ABS(actors[l]->y - a->y) < 32) {
          destroyActor(actors[l]);
          break;
        }
      }
    }
  } else if (a->vx != 0) {
    if (frames == 0) al_play_sample(stepSound, 1, 0, 0.8 + ((random() % 3) * 0.1), ALLEGRO_PLAYMODE_ONCE, NULL);
    frames += d;
    if (frames < 0.3) a->graphic = 161;
    else if (frames < 0.5) a->graphic = 162;
    else if (frames < 0.8) {
      a->graphic = 163;
      if (!sound) al_play_sample(stepSound, 1, 0, 0.8 + ((random() % 3) * 0.1), ALLEGRO_PLAYMODE_ONCE, NULL);
      sound = 1;
    } else a->graphic = 164;
    if (frames > 1) {
      frames -= 1;
      sound = 0;
      al_play_sample(stepSound, 1, 0, 0.8 + ((random() % 3) * 0.1), ALLEGRO_PLAYMODE_ONCE, NULL);
    }
  } else {
    frames = 0;
    a->graphic = 160;
  }
}

void renderPlayer(Actor a, float d) {
  if (lasering && battery > 0) {
    //draw laser effect
    int i;
    int coef = (a->hFlip) ? -1 : 1;
    int mod = (a->hFlip) ? 8 : 0;
    int flip = (a->hFlip) ? ALLEGRO_FLIP_HORIZONTAL : 0;
    static float frames = 0;
    frames += d;
    if (frames > 0.4) frames -= 0.4;
    for (i = 0; i < 512; i++) {
      if (frames < 0.2) al_draw_bitmap(graphics[194], a->x + coef * (mod + 12 + i * 8), a->y - 4, flip);
      else al_draw_bitmap(graphics[195], a->x + coef * (mod + 12 + i * 8), a->y - 4, flip);
    }
    if (frames < 0.2) al_draw_bitmap(graphics[192], a->x + coef * (mod + 4), a->y - 4, flip);
    else al_draw_bitmap(graphics[193], a->x + coef * (mod + 4), a->y - 4, flip);

    drawSpeechBubble(a, ":D!");
  } else if (lasering) {
    drawSpeechBubble(a, NO_LASER);
  }
}

void destroyedPlayer(Actor a) {
  player = NULL;
  makeActor(a->x, a->y, -15, 25, 0, 0, 0, 164, 0, 1, 0, NULL, NULL, NULL, NULL, NULL);
  al_play_sample(stopSound, 1, 0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
}


void tickWinSpot(Actor a, float d) {
  static float frames = 0;
  frames += d;
  if (frames < 0.3) a->graphic = 208;
  else if (frames < 0.66) a->graphic = 209;
  else a->graphic = 210;
  if (frames > 1) frames -= 1;
  if (player == NULL) return;
  if (abs(player->x - a->x) + abs(player->y - a->y) < 16) {
    destroyActor(player);
    wonGame = 1;
  }
}

void tickBadSpot(Actor a, float d) {
  static float frames = 0;
  frames += d;
  if (frames < 0.3) a->graphic = 224;
  else if (frames < 0.66) a->graphic = 225;
  else a->graphic = 226;
  if (frames > 1) frames -= 1;
  if (player == NULL) return;
  if (abs(player->x - a->x) + abs(player->y - a->y) < 6) {
    destroyActor(player);
  }
}

void renderWinSpot(Actor a, float d) {
  if (wonGame) {
    char* str = calloc(sizeof(char), 64);
    sprintf(str, "You win in just %f seconds.", running);
    drawSpeechBubble(a, str);
    free(str);
  }
}


void tickHeli(Actor a, float d) {
  if (player != NULL) {
    if (heliTime < 10) heliTime += d;
    else a->vx = -30;
  }
}

void renderHeli(Actor a, float d) {
  //draw rest of heli
  al_draw_bitmap(graphics[201], a->x - 20, a->y - 4, 0);
  al_draw_bitmap(graphics[202], a->x - 12, a->y - 4, 0);
  al_draw_bitmap(graphics[204], a->x + 4, a->y - 4, 0);
  al_draw_bitmap(graphics[205], a->x + 12, a->y - 4, 0);
  al_draw_bitmap(graphics[217], a->x - 20, a->y + 4, 0);
  al_draw_bitmap(graphics[218], a->x - 12, a->y + 4, 0);
  al_draw_bitmap(graphics[219], a->x - 4, a->y + 4, 0);
  al_draw_bitmap(graphics[220], a->x + 4, a->y + 4, 0);
  al_draw_bitmap(graphics[221], a->x + 12, a->y + 4, 0);
  if (player != NULL && heliTime < 10) {
    char* str = calloc(sizeof(char), 16);
    sprintf(str, "%f", 10 - heliTime);
    drawSpeechBubble(a, str);
    free(str);
  }
}

void tickDoor(Actor a, float d) {
  if (player == NULL) return;
  //if (abs(a->x - player->x) < 9 && abs(a->y - player->y) < 3) destroyActor(player);
}

void destroyedDoor(Actor a) {
  al_play_sample(startSound, 1, 0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
  makeActor(a->x, a->y, 0, 0, 0, 0, 0, 167, 1, 0, 0, NULL, NULL, NULL, NULL, NULL);
}

int main (int argc, char** argv) {
  al_init();
  al_install_keyboard();
  al_install_audio();
  al_init_acodec_addon();
  al_reserve_samples(5);
  al_init_image_addon();

  display = al_create_display(512, 512);
  al_set_window_title(display, "LD27: Ten Seconds of LASER");

  pthread_t eventThread;
  pthread_attr_t eventThreadAttr;
  
  pthread_attr_init(&eventThreadAttr);
  pthread_create(&eventThread, &eventThreadAttr, _eventThread, NULL);
  //"load" resources
  srandom(time(0));
  levelWidth = 64;
  levelHeight = 64;
  int r = levelWidth * levelHeight;

  spritesheet = al_load_bitmap("ldgfx.bmp");
  int i;
  graphics = malloc(sizeof(ALLEGRO_BITMAP*) * 256);
  for (i = 0; i < 256; i++) {
   graphics[i] = al_create_sub_bitmap(spritesheet, (i % 16) * 8, (i / 16) * 8,
                                       8, 8);
  }

  stepSound = al_load_sample("step.wav");
  laserSound = al_load_sample("laser.wav");
  kbdSound = al_load_sample("kbd.wav");
  startSound = al_load_sample("startup.wav");
  stopSound = al_load_sample("shutdown.wav");
  bweepSound = al_load_sample("bweep.wav");

  //initial actors
  //backdrop collision proxy actors lolo
  for (i = 0; i < 4096; i++) {
    if (level[i] == 166) {//door
      makeActor((i % levelWidth) * 8 + 4, (i / levelWidth) * 8 + 4, 0, 0, 0, 0, 0, 166, 0, 0, 1, tickDoor, NULL, destroyedDoor, NULL, NULL);
      level[i] = 14;
    } else if (level[i] == 208) { //win spot
      makeActor((i % levelWidth) * 8 + 4, (i / levelWidth) * 8 + 4, 0, 0, 0, 0, 0, 208, 0, 0, 0, tickWinSpot, NULL, NULL, renderWinSpot, NULL);
      level[i] = 14;
    } else if (level[i] == 224) { //enemy
      makeActor((i % levelWidth) * 8 + 4, (i / levelWidth) * 8 + 4, 0, 0, 0, 0, 0, 208, 0, 0, 0, tickBadSpot, NULL, NULL, NULL, NULL);
      level[i] = 14;
    } else if (level[i] > 127) {
      Actor a = makeActor((i % levelWidth) * 8 + 4, (i / levelWidth) * 8 + 4, 0, 0, 0, 0, 0, 00, 1, 0, 1, NULL, NULL, NULL, NULL, NULL);
      a->hidden = 1;
    }
  }

  //player and other stuff
  //helicopter with heli tick
  makeActor(256, 80, 0, 0, 0, 0, 0, 203, 0, 0, 0, tickHeli, NULL, NULL, renderHeli, NULL);
  //also end point
  //also enemies
  //clock actor to track time

  float delta, last, current;
  last = ((float)clock()) / (CLOCKS_PER_SEC);
  while (!quit) {
    current = ((float)clock()) / (CLOCKS_PER_SEC);
    delta = current - last;
    last = current;
    if (player != NULL) running += delta;
    for (i = 0; i < actorsCapacity; i++) {
      if (actors[i] == NULL) continue;
      if (actors[i]->fresh) actors[i]->fresh = 0;
      else {
        //printf("%d %f %f\n", actors[i]->graphic, actors[i]->y, actors[i]->vy);
        if (!(actors[i]->isStatic)) {

          if (actors[i]->physics) {
            actors[i]->vy -= 98 * delta;
          }
          actors[i]->vy += actors[i]->ay * delta;
          actors[i]->vx += actors[i]->ax * delta;

          if (actors[i]->collision) {
            //check collision before moving
            //TODO FIXME TODO FIXME TODO FIXME NYI
            Actor block = NULL;
            float dx = actors[i]->vx * delta * PIXELS_PER_METER;
            float dy = -(actors[i]->vy * delta * PIXELS_PER_METER);
            int j;
            float fat = 9;
            if (actors[i] == player && downing) fat = 4;
            for (j = 0; j < actorsCapacity; j++) {
              int vc = 0, hc = 0;
              if (actors[j] == NULL) continue;
              if (!(actors[j]->collision)) continue;
              if (actors[i] == actors[j]) continue;
              if (abs(actors[i]->x - actors[j]->x + dx) < fat) hc = 1;
              if (abs(actors[i]->y - actors[j]->y + dy) < 8) vc = 1;
              float distx = pow(actors[i]->x - actors[j]->x, 2);
              float disty = pow(actors[i]->y - actors[j]->y, 2);
              if (vc && hc && distx < 16 && disty < 64) { //stuck :(
                block = actors[j];
                //block->graphic += 1; //FIXME
                dx = 0; dy = 0;
                break;
                //printf("actor %d collided with %d.\n", actors[i]->graphic, actors[j]->graphic);
              }

              if (vc && distx < fat * 8) {
                if (actors[i]->y > actors[j]->y) dy = MAX(MIN(actors[i]->y - actors[j]->y + 8, dy), 0);
                else dy = MIN(MAX(actors[j]->y - actors[i]->y + 8, dy), 0);
              }

              if (hc && disty < 56) {
                if (actors[i]->x > actors[j]->x) dx = MAX(MIN(actors[i]->x - actors[j]->x + 8, dx), 0);
                else dx = MIN(MAX(actors[j]->x - actors[i]->x - 8, dx), 0);
              }
            }

            if (dy == 0) actors[i]->vy = 0;
            if (dx == 0) actors[i]->vx = 0;
            actors[i]->y += dy;
            actors[i]->x += dx;

            if (actors[i]->x < -8 || actors[i]->x > 520 || actors[i]->y < -8 ||
                actors[i]->y > 520) {
              destroyActor(actors[i]);
              continue;
            }

          } else {
            actors[i]->x += actors[i]->vx * delta * PIXELS_PER_METER;
            actors[i]->y -= actors[i]->vy * delta * PIXELS_PER_METER;
            if (actors[i]->x < -8 || actors[i]->x > 520 || actors[i]->y < -8 ||
                actors[i]->y > 520) {
              destroyActor(actors[i]);
              continue;
            }
          }

        }

        if (actors[i]->tick != NULL) actors[i]->tick(actors[i], delta);
      }
    }

    //render background
    //TODO modify to check distance from player for visibility on certain tiles
    for (i = 0; i < r; i++) {
      float k = tintMult(i);
      al_draw_tinted_bitmap(graphics[level[i]], al_map_rgb_f(k, k, k), (i % levelWidth) * 8,
                     (i / levelHeight) * 8, 0);
    }
    
    //render actors
    for (i = 0; i < actorsCapacity; i++) {
      //TODO modify to check distance from player hacky visibility
      if (actors[i] != NULL) drawActor(actors[i], delta);
    }

    //need rendering functions for effects
    //speech bubble
    //if (player != NULL) drawSpeechBubble(player, "test!");
    //strings
    //laser

    al_flip_display();
  }

  pthread_join(eventThread, NULL);

  al_destroy_display(display);
  al_uninstall_audio();
  al_uninstall_keyboard();
  for (i = 0; i < 256; i++) al_destroy_bitmap(graphics[i]);
  free(graphics);
  al_destroy_bitmap(spritesheet);

  if (actors != NULL) free(actors);

  return 0;
}
