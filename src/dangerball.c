////////////////////////////////
//
//
// THE PLAN
// - remove my easings and use reasings.h instead
// - improve character moving, kick animation, and bumping between characters until it feels good
// - maybe start the implementation of a more robust animation system, maybe with an editor (raygui)
// - improve level generation and transitions (maybe fixed camera levels)
// - add grappling gun and items (gps, a new tape maybe, magnet, boxing glove to the gun, air blower)
// - new backgrouds (maybe betting system)
// - tons of hitstop, maybe boss stop for the bomb
// - game still has to be responsive during animation, so I have to implement a more general state machine
//
//
////////////////////////////////

#include "raylib.h"
#include "include/raymath.h"
#include <stdlib.h>

#define GLSL_VERSION 330
#define Global_Variable static
#define Local_Persist static

typedef struct Matrix2x2
{
    float m0, m2;
    float m1, m3;
}Matrix2;

typedef struct Player
{
    Rectangle sprite_source_rec;
    Rectangle sprite_dest_rec;
    Vector2 velocity;
    Vector2 pos;
    Vector2 sprite_origin;
    Color color;
    
    float speed;
    float normal_speed;
    float slow_speed;
    float angle;
    float sprite_angle;
    float stick_angle;
    float timer;
    int controller;
    int frame;
    enum {STANDING, RUNNING, DROPKICKING, HITSTUN, ITEM} state;
    enum {NO_ITEM = 0, TAPE, GPS} item;
    enum {SAFE, DANGER} gps_result;
    int ready;
    
    float tape_length;
    float gps_timer;
    
    float hurtbox_radius;
    Vector2 hitbox_relative_pos;
    float kick_intensity;
    
    float kick_timer;
    float sin_timer;
    float item_timer;
    float hitstun;
    
    Vector2 tape_offset;
    Vector2 tape_end;
    
    Texture2D* run_sprite;
    Texture2D* dropkick_sprite;
    Texture2D* measuring_sprite;
    Texture2D* tape_sprite;
    Texture2D* gps_sprite;
    
} Player;

typedef struct Ball
{
    Vector2 pos;
    Vector2 velocity;
    float speed;
    float radius;
    float explosion_radius;
    
} Ball;

typedef struct Item_Pickup
{
    Vector2 pos;
    int active;
    float animation;
    enum {ITEM_INACTIVE, ITEM_TAPE, ITEM_GPS} item;
} Item_Pickup;

typedef struct Match_Layout
{
    int number_of_items;
    int number_of_players;
    Item_Pickup* items;
    Vector2 bomb_position;
    Vector2* player_positions;
    // TODO(matheus): maybe add a pointer to a background (union maybe?)
} Match_Layout;


typedef struct Match_Settings
{
    float bomb_timer;
    float bomb_radius;
    Match_Layout layout;
} Match_Settings;


typedef struct
{
    float a;
    float b;
    float c;
} Animation_Data;

Global_Variable 
struct Checkered_Pattern
{
    Texture2D* texture;
    Rectangle* source_rect;
    Rectangle* dest_rect;
    Vector2* offset;
} checkered_pattern;

typedef enum Easings {LINEAR, CUBIC, EXPONENTIAL} Easing;

enum Textures {TEX_PLAYER, TEX_DROPKICKING, TEX_MEASURING, TEX_TAPE, TEX_GPS, TEX_COUNT};

Global_Variable int match_over = 0;
Global_Variable Player player1, player2;
Global_Variable Ball g_ball;

Global_Variable Shader* global_pixel_shader;

Global_Variable Camera2D global_camera = { 0 };
Global_Variable float global_camera_rect_area;

Global_Variable const int screenWidth = 1680;
Global_Variable const int screenHeight = 960;

//temporary
float ktmul = 7.45f;
float dkmin = 220.0f;
float dkmax = 1920.0f;
float dkfinal = 1050.0f;


void ItemPickupRender(Item_Pickup* pickup)
{
    float y_pos_animation = pickup->pos.y + (sinf(pickup->animation * 2.5f) * 30.0f);
    DrawRectanglePro((Rectangle) {pickup->pos.x, y_pos_animation, 50.0f, 50.0f}, (Vector2) {25.0f, 25.0f}, pickup->animation * 100.0f, ORANGE);
}

Matrix2 Matrix2Rotate(float angle)
{
    Matrix2 result;
    
    result.m0 = cosf(angle);
    result.m2 = -sinf(angle);
    result.m1 = sinf(angle);
    result.m3 = cosf(angle);
    
    return result;
}

Vector2 MatrixVectorMul(Matrix2 m, Vector2 v)
{
    Vector2 v1;
    v1.x = v.x * m.m0 + v.y * m.m2;
    v1.y = v.x * m.m1 + v.y * m.m3;
    return v1;
}

float LerpDelta(float start, float end, float rate, float dt)
{
    return Lerp(end, start, exp2f(-rate*dt));
}

float Smoothstep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}


float EaseOutCubic(float min, float max, float value)
{
    value = Clamp(value, 0.0f, 1.0f);
    value = 1.0f - ((float) pow(1.0f - value, 3.0f));
    
    return Lerp(min, max, value);
}

float EaseOutCubicDelta(float min, float max, float value, float dt)
{
    value = Clamp(value, 0.0f, 1.0f);
    value = 1.0f - ((float) pow(1.0f - value, 3.0f));
    value = -(1.0f/dt)*log2f(1.0f - value);
    
    return LerpDelta(min, max, value, dt);
}

float KickArc(float t)
{
    float x = 2.1f * Lerp(Smoothstep(0.2f, 0.7f, t), Smoothstep(0.4f, 1.0f, 1.0f - t), Smoothstep(0.2f, 1, t));
    return x;
}

void DropkickUpdate(Player* player, float dt)
{
    player->kick_timer += dt * ktmul;
    float kick_velocity;
    if(player->kick_timer > 0.5)
        kick_velocity = Lerp(dkmin, dkmax, KickArc(player->kick_timer));
    else
        kick_velocity = Lerp(dkfinal, dkmax, KickArc(player->kick_timer));
    
    player->speed = kick_velocity;
}

int CircleCollisionDetect(Vector2 center_a, float radius_a, Vector2 center_b, float radius_b)
{
    return Vector2Length(Vector2Subtract(center_a, center_b)) < radius_a + radius_b;
}

Vector2 CircleCollisionResolve(Vector2 center_a, float radius_a, Vector2 center_b, float radius_b)
{
    return Vector2Add(center_a, Vector2Scale(Vector2Normalize(Vector2Subtract(center_b, center_a)), radius_a + radius_b));
}

Vector2 CircleCollisionGetResolution(Vector2 center_a, float radius_a, Vector2 center_b, float radius_b)
{
    return Vector2Scale(Vector2Normalize(Vector2Subtract(center_b, center_a)), radius_a + radius_b);
}

void PlayerCollisionsUpdate(float dt)
{
    if(CircleCollisionDetect(player1.pos, 40.0f, player2.pos, 40.0f))
    {
        if(player1.state != DROPKICKING && player2.state != DROPKICKING)
        {
            while(CircleCollisionDetect(player1.pos, 40.0f, player2.pos, 40.0f))
            {
                Vector2 p1_resolution_vector = Vector2Scale(player1.velocity, -1.0f);
                player1.pos.x += p1_resolution_vector.x * player1.speed * dt;
                player1.pos.y += p1_resolution_vector.y * player1.speed * dt;
                
                Vector2 p2_resolution_vector = Vector2Scale(player2.velocity, -1.0f);
                player2.pos.x += p2_resolution_vector.x * player2.speed * dt;
                player2.pos.y += p2_resolution_vector.y * player2.speed * dt;
            }
        }
        else
        {
            float p1_speed = Vector2Length(player1.velocity) * player1.speed;
            float p2_speed = Vector2Length(player2.velocity) * player2.speed;
            
            Vector2 resolution_vector = CircleCollisionGetResolution(player1.pos, 40.0f, player2.pos, 40.0f);
            
            player2.pos = Vector2Add(player2.pos, Vector2Scale(resolution_vector, 0.5f));
            player1.pos = Vector2Add(player1.pos, Vector2Scale(resolution_vector, -0.5f));
            
            player2.velocity = Vector2Normalize(Vector2Subtract(player2.pos, player1.pos));
            
            player1.velocity = Vector2Scale(player2.velocity, -1.0f);
            
            player1.speed = p2_speed;
            player2.speed = p1_speed;
            
            if(player1.state == DROPKICKING && player2.state != DROPKICKING)
            {
                player2.state = HITSTUN;
                player2.hitstun = 0.4f;
            }
            if(player2.state == DROPKICKING && player1.state != DROPKICKING)
            {
                player1.state = HITSTUN;
                player1.hitstun = 0.4f;
            }
        }
    }
}

void BallInit(Ball* ball, float x, float y, float explosion_radius)
{
    ball->pos.x = x;
    ball->pos.y = y;
    ball->explosion_radius = explosion_radius;
    
    ball->radius = 30.0f;
    ball->velocity.x = 0.0f;
    ball->velocity.y = 0.0f;
    ball->speed = 0.0f;
}

void BallUpdate(Ball* ball, float dt)
{
    ball->pos.x += ball->velocity.x * ball->speed * dt;
    ball->pos.y += ball->velocity.y * ball->speed * dt;
    
    ball->speed -= ball->speed * 0.9f * dt;
    
    if(ball->speed < 40.0f)
        ball->speed = 0.0f;
}

void PlayerInit(Player* player, float initial_x, float initial_y, Texture2D* sprites,  Color color)
{
    player->pos.x = initial_x;
    player->pos.y = initial_y;
    
    player->state = STANDING;
    player->speed = 600.0f;
    player->angle = 0.0f;
    player->stick_angle = 0.0f;
    player->timer = 0.0f;
    player->sprite_source_rec.x = 0.0f;
    player->sprite_source_rec.y = 0.0f;
    player->sprite_source_rec.width = 64.0f;
    player->sprite_source_rec.height = 32.0f;
    player->sprite_dest_rec.x = initial_x;
    player->sprite_dest_rec.y = initial_y;
    player->sprite_dest_rec.width = player->sprite_source_rec.width * 5.0f;
    player->sprite_dest_rec.height = player->sprite_source_rec.height * 5.0f;
    player->color = color;
    player->sprite_origin.x = 160.0f;
    player->sprite_origin.y = 80.0f;
    player->frame = 3;
    
    // NOTE(matheus): The tape sprite is at x=50, y=10 from the rect origin
    player->tape_offset.x = 50.0f * 5.0f - player->sprite_origin.x;
    player->tape_offset.y = 10.0f * 5.0f - player->sprite_origin.y;
    
    player->sin_timer = 0.0f;
    player->kick_timer = 0.0f;
    player->item_timer = 0.0f;
    
    
    player->normal_speed = 600.0f;
    player->slow_speed = 150.0f;
    
    
    
    player->run_sprite = &sprites[TEX_PLAYER];
    player->dropkick_sprite = &sprites[TEX_DROPKICKING];
    player->measuring_sprite = &sprites[TEX_MEASURING];
    player->tape_sprite = &sprites[TEX_TAPE];
    player->gps_sprite = &sprites[TEX_GPS];
    
    player->tape_length = 500.0f;
    player->item = NO_ITEM;
    
}

void PlayerItemInit(Player* player)
{
    switch(player->item)
    {
        case TAPE:
        {
            Vector2 rotated_tape_pos = MatrixVectorMul(Matrix2Rotate(player->angle), player->tape_offset);
            rotated_tape_pos= Vector2Add(player->pos, rotated_tape_pos); 
            player->tape_end = rotated_tape_pos;
            
        } break;
        case GPS:
        {
            
        } break;
        default:
        {
            
        }
    }
}

void PlayerItemStateUpdate(Player* player, float dt)
{
    Vector2 stick_input = (Vector2){GetGamepadAxisMovement(player->controller, 0), GetGamepadAxisMovement(player->controller, 1)};
    
    player->velocity = stick_input;
    
    if(GetGamepadAxisMovement(player->controller, 4) > 0)
    {
        player->speed = player->slow_speed;
    }
    else
    {
        player->speed = player->normal_speed;
    }
    
    switch(player->item)
    {
        case TAPE:
        {
            Vector2 player_to_line_end = Vector2Subtract(player->pos, player->tape_end);
            player->angle = atan2f(player_to_line_end.y, player_to_line_end.x) + PI;
            
            if(IsGamepadButtonPressed(player->controller, GAMEPAD_BUTTON_RIGHT_FACE_UP))
            {
                player->state = STANDING;
            }
        } break;
    }
}

void PlayerAngleUpdate(Player* player, float dt)
{
    Vector2 stick_input = (Vector2){GetGamepadAxisMovement(player->controller, 0), GetGamepadAxisMovement(player->controller, 1)};
    
    float player_angle = player->angle;
    float input_angle = atan2f(stick_input.y, stick_input.x);
    float converted_input_angle = input_angle;
    float converted_player_angle = player->angle;
    float final_angle;
    
    float rate = -(1.0f/dt)*log2(0.85);
    
    if(input_angle < 0.0f)
    {
        converted_input_angle = 2.0f*PI + input_angle;
    }
    if(player_angle < 0.0f)
    {
        converted_player_angle = 2.0f*PI + player_angle;
    }
    
    float angle_distance = fabsf(converted_player_angle - converted_input_angle);
    
    //measure counter-clockwise rotation
    if (angle_distance <= 0.2f)
    {
        final_angle = input_angle;
    }
    else if(angle_distance <= PI)
    {
        //should rotate counter-clockwise
        //aka lerp player to a positive input_angle
        
        final_angle = LerpDelta(converted_player_angle, converted_input_angle, rate, dt);
    }
    else
    {
        //should rotate clockwise
        //undo conversions and lerp
        final_angle = LerpDelta(player_angle, input_angle, rate, dt);
    }
    
    if(final_angle > PI)
    {
        final_angle = final_angle - 2.0f*PI;
    }
    
    player->angle = final_angle;
}

void PlayerStateUpdate(Player* player, float dt)
{
    player->timer += dt;
    Vector2 stick_input = (Vector2){GetGamepadAxisMovement(player->controller, 0), GetGamepadAxisMovement(player->controller, 1)};
    
    switch(player->state)
    {
        case STANDING:
        {
            player->frame = 3;
            player->timer = 0.0f;
            player->sin_timer = 0.0f;
            player->velocity = stick_input;
            
            if(IsGamepadButtonPressed(player->controller, GAMEPAD_BUTTON_RIGHT_FACE_UP) && player->item)
            {
                player->state = ITEM;
                PlayerItemInit(player);
            }
            else if(player->velocity.x || player->velocity.y)
            {
                player->state = RUNNING;
            }
        } break;
        
        case RUNNING:
        {
            player->velocity = stick_input;
            
            player->sin_timer += dt;
            //player->angle += sinf(player->sin_timer * 5.0f * PI) * 5.0f;
            
            
            if(GetGamepadAxisMovement(player->controller, 4) > 0)
                player->speed = player->slow_speed;
            else
                player->speed = player->normal_speed;
            
            
            if(!player->velocity.x && !player->velocity.y)
            {
                player->state = STANDING;
            }
            else if(IsGamepadButtonPressed(player->controller, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
            {
                player->state = DROPKICKING;
                player->angle = atan2f(stick_input.y, stick_input.x);
                player->timer = 0.0f;
                player->frame = 0;
                player->sin_timer = 0.0f;
            }
            else if(IsGamepadButtonPressed(player->controller, GAMEPAD_BUTTON_RIGHT_FACE_UP) && player->item)
            {
                player->state = ITEM;
                PlayerItemInit(player);
            }
            else
            {
                while(player->timer > 0.1f)
                {
                    player->timer -= 0.1f;
                    player->frame += 1;
                    
                    if(player->frame > 3) {player->frame = 0;}
                }
                
                PlayerAngleUpdate(player, dt);
            }
        } break;
        
        case DROPKICKING:
        {
            while(player->timer > 0.1f)
            {
                player->timer -= 0.1f;
                player->frame += 1;
                
                if(player->frame > 11) 
                {
                    player->state = STANDING;
                    player->timer = 0.0f;
                    player->frame = 3;
                    player->kick_timer = 0.0f;
                }
                else
                {
                    DropkickUpdate(player, dt);
                }
            }
        } break;
        
        case HITSTUN:
        {
            player->hitstun -= dt;
            if(player->hitstun <= 0.0f)
            {
                player->state = STANDING;
                player->timer = 0.0f;
                player->sin_timer = 0.0f;
                player->frame = 3;
            }
        } break;
        
        case ITEM:
        {
            PlayerItemStateUpdate(player, dt);
        } break;
        
    }
    
}


void PlayerPositionUpdate(Player* player, float dt, Vector2 up_left, Vector2 down_right)
{
    if(player->velocity.x != 0 && player->velocity.y != 0)
    {
        player->velocity = Vector2Normalize(player->velocity);
    }
    
    player->pos.x += player->velocity.x * player->speed * dt;
    player->pos.y += player->velocity.y * player->speed * dt;
    
    player->pos.x = Clamp(player->pos.x,up_left.x + player->hurtbox_radius * 1.1f, down_right.x - player->hurtbox_radius * 1.1f); 
    player->pos.y = Clamp(player->pos.y,up_left.y + player->hurtbox_radius * 1.1f, down_right.y - player->hurtbox_radius * 1.1f); 
    
    player->sprite_dest_rec.x = player->pos.x;
    player->sprite_dest_rec.y = player->pos.y;
    
    if(player->state == ITEM && player->item == TAPE)
    {
        Vector2 player_to_tape = Vector2Subtract(player->tape_end, player->pos);
        
        float distance_from_tape_end = Vector2Length(player_to_tape);
        
        if(distance_from_tape_end > player->tape_length)
        {
            float difference = distance_from_tape_end - player->tape_length;
            player->pos = Vector2Add(player->pos, Vector2Scale(Vector2Normalize(player_to_tape), difference));
        }
        
    }
    
}

void PlayerBallCollisionResolve(Player* player)
{
    if(CircleCollisionDetect(player->pos, 40.0f, g_ball.pos, g_ball.radius))
    {
        g_ball.pos = CircleCollisionResolve(player->pos, 40.0f, g_ball.pos, g_ball.radius);
        
        g_ball.velocity = Vector2Normalize(Vector2Subtract(g_ball.pos, player->pos));
        
        if(Vector2Length(player->velocity) != 0.0f)
        {
            g_ball.speed = Vector2Length(player->velocity) * player->speed * 1.1f;
        }
    }
}

void PlayerRender(Player* player)
{
    player->sprite_source_rec.x = player->sprite_source_rec.width * player->frame;
    
    switch(player->state)
    {
        case DROPKICKING:
        {
            DrawTexturePro(*player->dropkick_sprite,player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, player->angle * RAD2DEG, player->color);
        } break;
        
        case ITEM:
        {
            if(player->item == TAPE)
            {
                Vector2 rotated_tape_pos = MatrixVectorMul(Matrix2Rotate(player->angle), player->tape_offset);
                rotated_tape_pos= Vector2Add(player->pos, rotated_tape_pos); 
                
                DrawLineEx(rotated_tape_pos, player->tape_end, 10.0f, GOLD);
                
                DrawTexturePro(*player->tape_sprite, player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, player->angle * RAD2DEG, WHITE);
                
                DrawTexturePro(*player->measuring_sprite, player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, player->angle * RAD2DEG, player->color);
                
                Vector2 tape_info_pos;
                
                tape_info_pos.x = 50.0f * 5.0f - player->sprite_origin.x;
                tape_info_pos.y = -5.0f * 5.0f - player->sprite_origin.y;
                
                tape_info_pos = MatrixVectorMul(Matrix2Rotate(player->angle), tape_info_pos);
                tape_info_pos = Vector2Add(player->pos, tape_info_pos);
                
                //DrawCircleV(tape_info_pos, 40.0f, GOLD);
                // TODO(matheus): gotta implement a hud drawing function
                DrawText(TextFormat("%.2f", Vector2Length(Vector2Subtract(player->pos, player->tape_end))), tape_info_pos.x - 40.0f, tape_info_pos.y - 15.0f, 30,  WHITE);
            }
            else if(player->item == GPS)
            {
                
                DrawTexturePro(*player->gps_sprite, player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, (player->angle) * RAD2DEG, WHITE);
                
                Color color = player->gps_result == SAFE ? GREEN : RED; 
                
                DrawTexturePro(*player->measuring_sprite, player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, (player->angle) * RAD2DEG, color);
            }
            
            
        } break;
        
        case RUNNING:
        {
            float render_angle = (player->angle * RAD2DEG) + sinf(player->sin_timer * 5.0f * PI) * 15.0f;
            
            DrawTexturePro(*player->run_sprite, player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, render_angle, player->color);
        } break;
        
        default:
        {
            DrawTexturePro(*player->run_sprite, player->sprite_source_rec, player->sprite_dest_rec, player->sprite_origin, (player->angle) * RAD2DEG, player->color);
        } break;
        
    }
    
    //DrawRectanglePro(temp_rectangle, (Vector2) {player->hurtbox_rec.width * 0.5f, player->hurtbox_rec.height * 0.5f}, player->angle-90.0f, hurtbox_color);
    
    //DrawRectangle(-5.0f + player2.pos.x + resolution.x * 35.0f, -5.0f + player2.pos.y + resolution.y * 35.0f, 10.0f, 10.0f, BLUE);
    
    
    //DrawCircleV(player->pos, player->hurtbox, hurtbox_color); 
}

void PlayerHUDRender(Player *player, Camera2D* camera)
{
    
}

void CameraSetTargetAndZoom(Camera2D* camera, Vector2* entities, int number_of_entities, float dt)
{
    float left = entities[0].x; 
    float right = entities[0].x; 
    float top = entities[0].y; 
    float bottom = entities[0].y; 
    
    for(int i = 1; i < number_of_entities; ++i)
    {
        if(entities[i].x < left)
        {
            left = entities[i].x;
        }
        else if(entities[i].x > right)
        {
            right = entities[i].x;
        }
        
        if(entities[i].y < top)
        {
            top = entities[i].y;
        }
        else if(entities[i].y > bottom)
        {
            bottom = entities[i].y;
        }
    }
    
    camera->target.x = LerpDelta(camera->target.x, left + (right - left)/2.0f, 3.5f, dt);
    camera->target.y = LerpDelta(camera->target.y, top + (bottom - top)/2.0f, 3.5f, dt);
    
    float camera_rect_area = fmaxf(right - left, bottom - top) * 0.0003f;
    global_camera_rect_area = camera_rect_area;
    float desired_zoom = EaseOutCubic(1.45f, 0.24f, camera_rect_area);
    desired_zoom = LerpDelta(camera->zoom, desired_zoom, 3.0f, dt);
    
    camera->zoom = Clamp(desired_zoom, 0.20f, 2.0f);
    
    
}

void CameraResolveBoundaries(Camera2D* camera, Vector2 up_left, Vector2 down_right, Rectangle screen)
{
    Vector2 upper_left_corner = GetWorldToScreen2D(up_left, *camera);
    Vector2 lower_right_corner = GetWorldToScreen2D(down_right, *camera);
    
    
    
    if(upper_left_corner.x > screen.x)
    {
        camera->offset.x -= upper_left_corner.x;
    }
    else if(lower_right_corner.x < screen.x + screen.width)
    {
        camera->offset.x += (screen.x + screen.width) - lower_right_corner.x;
    }
    
    if(upper_left_corner.y > screen.y)
    {
        camera->offset.y -= upper_left_corner.y;
    }
    else if(lower_right_corner.y < screen.y + screen.height)
    {
        camera->offset.y += (screen.y + screen.height) - lower_right_corner.y;
    }
    
    
    
}

void BallRender(void)
{
    Color color = YELLOW;
    
    if(!match_over)
    {
        DrawCircle(g_ball.pos.x, g_ball.pos.y, g_ball.radius, color);
    }
    else
    {
        DrawCircle(g_ball.pos.x, g_ball.pos.y, g_ball.explosion_radius, (Color) {255, 100, 100, 80});
    }
}

void TestMatchGenerate(Match_Layout* test_match)
{
    test_match->number_of_items = 1;
    test_match->items = malloc(sizeof(Item_Pickup));
    test_match->items[0].pos = (Vector2) {0.0f, -300.0f};
    test_match->items[0].animation = 0.0f;
    test_match->items[0].item = TAPE;
    test_match->items[0].active = 1;
}

void TexturesLoad(Texture2D* textures)
{
    textures[TEX_PLAYER] = LoadTexture("C:/work/DangerBall/assets/dangerball-run.png");
    
    textures[TEX_DROPKICKING] = LoadTexture("C:/work/DangerBall/assets/dangerball-dropkick.png");
    
    textures[TEX_MEASURING] =
        LoadTexture("C:/work/DangerBall/assets/dangerballmeasure.png");
    
    textures[TEX_TAPE] = LoadTexture("C:/work/DangerBall/assets/measuretape.png");
    
    textures[TEX_GPS] = LoadTexture("C:/work/DangerBall/assets/gps.png");
    
}

void GameRender(Match_Layout* current_match)
{
    static int debug_toggle = 0;
    
    if(IsKeyReleased(KEY_Z)){debug_toggle = !debug_toggle;}
    
    ////////////////////////////////
    
    BeginMode2D(global_camera);
    
    BeginShaderMode(*global_pixel_shader);
    
    
    //ClearBackground(WHITE);
    
    
    DrawTexturePro(*checkered_pattern.texture, *checkered_pattern.source_rect, *checkered_pattern.dest_rect, *checkered_pattern.offset, 0.0f, WHITE);
    
    EndShaderMode();
    
    BallRender();
    
    for(int i = 0; i < current_match->number_of_items; i++)
    {
        if(current_match->items[i].active)
        {
            ItemPickupRender(&current_match->items[i]);
        }
    }
    
    
    PlayerRender(&player1);
    PlayerRender(&player2);
    
    //DrawCircle(-checked.width * 10.0f, 0.0 ,10.0, GOLD);
    
    EndMode2D();
    
    
    
    //DrawText(TextFormat("Explosion Radius: %.3f", g_ball.explosion_radius), -15 + screenWidth/2, screenHeight - 80, 40, RED);
    
    if(debug_toggle)
    {
        float stick_angle = atan2f(GetGamepadAxisMovement(player1.controller, 1), GetGamepadAxisMovement(player1.controller, 0));
        
        DrawText(TextFormat("PlayerVel: %.2f", Vector2Length(player1.velocity)), 10, 30, 20, WHITE);
        DrawText(TextFormat("PlayerSpeed: %.2f", player1.speed), 10, 50, 20, WHITE);
        DrawText(TextFormat("PlayerAngle: %.2f", player1.angle), 10, 70, 20, WHITE);
        DrawText(TextFormat("StickAngle: %.2f", stick_angle), 10, 90, 20, WHITE);
        DrawText(TextFormat("PlayerTimer: %.2f", player1.timer), 10, 110, 20, WHITE);
        
        
        DrawText(TextFormat("KickTimer: %.2f", player1.kick_timer), 10, 130, 20, WHITE);
        DrawText(TextFormat("KickArc: %.2f", KickArc(player1.kick_timer)), 10, 150, 20, WHITE);
        DrawText(TextFormat("KickTimerMul: %.2f", ktmul), 10, 170, 20, WHITE);
        DrawText(TextFormat("KickMinVel: %.2f", dkmin), 10, 190, 20, WHITE);
        DrawText(TextFormat("KickMaxVel: %.2f", dkmax), 10, 210, 20, WHITE);
        DrawText(TextFormat("KickFinalVel: %.2f", dkfinal), 10, 230, 20, WHITE);
        DrawText(TextFormat("g_ball.pos: x=%.2f y=%2.f", g_ball.pos.x, g_ball.pos.y), 10, 250, 20, WHITE);
        DrawText(TextFormat("Tape_Length: %.2f", Vector2Length(Vector2Subtract(player1.pos, player1.tape_end))), 10, 270, 20, WHITE);
        DrawText(TextFormat("camera_rect_area: %.2f", global_camera_rect_area), 10, 290, 20, WHITE);
        
        
        
        
        
    }
    
}


void MatchStartAnimationPlay(Match_Settings* current_match)
{
    const char* bomb_radius = TextFormat("Bomb Radius: %0.2f", current_match->bomb_radius);
    int bomb_radius_text_width = MeasureText(bomb_radius, 80);
    
    float animation_timer = 0.0f;
    float dt;
    
    while(animation_timer <= 6.0f)
    {
        dt = GetFrameTime();
        animation_timer += dt;
        
        BeginDrawing();
        GameRender(&current_match->layout);
        
        
        if(animation_timer >= 1.5f)
        {
            DrawText(bomb_radius, (screenWidth - bomb_radius_text_width)/2,
                     screenHeight/2 - 160, 80, GOLD);
        }
        
        EndDrawing();
    }
}

void MatchBombExplosionAnimationPlay(void)
{
    
}

void MatchResultsAnimationPlay(Match_Settings* current_match)
{
    float p1_distance = Vector2Length(Vector2Subtract(player1.pos, g_ball.pos)) - g_ball.explosion_radius;
    float p2_distance = Vector2Length(Vector2Subtract(player2.pos, g_ball.pos)) - g_ball.explosion_radius;
    
    char* p1_distance_text;
    char* p2_distance_text;
    char* match_result;
    
    if(p1_distance < 0.0f)
    {
        p1_distance_text = "DEAD";
    }
    else
    {
        p1_distance_text = TextFormat("%.2f", p1_distance); 
    }
    
    if(p2_distance <= 0.0f)
    {
        p2_distance_text = "DEAD";
    }
    else
    {
        p2_distance_text = TextFormat("%.2f", p2_distance); 
    }
    
    if(p1_distance > 0.0f && p2_distance > 0.0f)
    {
        if(p1_distance < p2_distance)
        {
            match_result = "Player 1 Wins"; 
        }
        else if(p2_distance < p1_distance)
        {
            match_result = "Player 2 Wins";
        }
        else
        {
            match_result = "Draw";
        }
    }
    else if(p1_distance <= 0.0f && p2_distance <= 0.0f)
    {
        match_result = "Draw";
    }
    else
    {
        if(p1_distance <= 0.0f)
        {
            match_result = "Player 2 Wins"; 
        }
        else
        {
            match_result = "Player 1 Wins";
        }
    }
    
    float animation_timer = 0.0f;
    float dt;
    
    while(animation_timer <= 8.5f)
    {
        dt = GetFrameTime();
        animation_timer += dt;
        
        BeginDrawing();
        GameRender(&current_match->layout);
        
        
        if(animation_timer >= 1.0f)
        {
            DrawText("Player 1 Distance:", (screenWidth - MeasureText("Player 1 Distance:", 60))/2,
                     160, 60, GOLD);
        }
        if(animation_timer >= 2.5f)
        {
            DrawText(p1_distance_text, (screenWidth - MeasureText(p1_distance_text, 60))/2,
                     220, 60, GOLD);
        }
        if(animation_timer >= 3.5f)
        {
            DrawText("Player 2 Distance:", (screenWidth - MeasureText("Player 2 Distance:", 60))/2,
                     280, 60, GOLD);
        }
        if(animation_timer >= 5.0f)
        {
            DrawText(p2_distance_text, (screenWidth - MeasureText(p2_distance_text, 60))/2,
                     350, 60, GOLD);
        }
        if(animation_timer >= 6.0f)
        {
            DrawText(match_result, (screenWidth - MeasureText(match_result, 80))/2,
                     410, 80, GOLD);
        }
        
        
        EndDrawing();
    }
    
}

void MatchSettingsInit(Match_Settings* settings)
{
    /*
    Match_Layout match_layout = layouts[0];
    
    settings->bomb_timer = (float)GetRandomValue(10,25);
    settings->bomb_radius = (float)GetRandomValue(300, 3000);
    
    settings->bomb_pos = match_layout.bomb_position;
    
    settings.items = malloc(sizeof(Item_Pickup) * match_layout.number_of_items);
    memcpy(settings.items, match_layout.items, sizeof(Item_Pickup) * match_layout.number_of_items);
    
    settings.number_of_item_pickups = match_layout.number_of_items;
    */
}

void MatchInit(Texture2D* textures, int* intro_animation_played, float* match_time, Match_Layout* test_match_ptr, Match_Settings* settings)
{
    global_camera.rotation = 0.0f;
    global_camera.zoom = 1.0f;
    global_camera.offset.x = screenWidth/2.0f;
    global_camera.offset.y = screenHeight/2.0f;
    
    PlayerInit(&player1, -300, 0, textures, BLACK);
    PlayerInit(&player2, 300, 0, textures, WHITE);
    
    player1.controller = 0;
    player2.controller = 1;
    
    player2.angle = PI;
    
    *intro_animation_played = 0;
    
    *match_time = 15.0f;
    
    settings->bomb_timer = *match_time;
    settings->bomb_radius = g_ball.explosion_radius;
    settings->layout = *test_match_ptr;
}

int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    //const int screenWidth = 1260;
    //const int screenHeight = 720;
    
    
    InitWindow(screenWidth, screenHeight, "DangerBall");
    SetWindowState(FLAG_VSYNC_HINT);
    
    Texture2D* textures = malloc(sizeof(Texture2D) * TEX_COUNT);
    
    TexturesLoad(textures);
    
    Match_Layout test_match = {0};
    TestMatchGenerate(&test_match);
    
    global_camera.rotation = 0.0f;
    global_camera.zoom = 1.0f;
    global_camera.offset.x = screenWidth/2.0f;
    global_camera.offset.y = screenHeight/2.0f;
    
    Vector2 random_point;
    random_point.x = (float) GetRandomValue(-200, 200);
    random_point.y = (float) GetRandomValue(-200, 200);
    
    BallInit(&g_ball, 0.0f, 0.0f, GetRandomValue(400, 2000));
    
    {
        //Image checkered_pattern = GenImageChecked(512, 512, 8, 8, RED, MAROON);
        Image generated_pattern = GenImageChecked(512, 512, 16, 16, BLUE, DARKBLUE);
        Texture2D* texture = malloc(sizeof(Texture2D));
        Rectangle* source = malloc(sizeof(Rectangle));
        Rectangle* dest = malloc(sizeof(Rectangle));
        Vector2* offset = malloc(sizeof(Vector2));
        
        *texture = LoadTextureFromImage(generated_pattern);
        *source = (Rectangle) {0.0f, 0.0f, texture->width, texture->height};
        *dest = (Rectangle) {-texture->width*10.0f, -texture->height*10.f, texture->width*20.0f, texture->height*20.f};
        *offset = (Vector2) {0.0f, 0.0f};
        
        checkered_pattern.texture = texture;
        checkered_pattern.source_rect = source;
        checkered_pattern.dest_rect = dest;
        checkered_pattern.offset = offset;
    }
    
    RenderTexture2D target = LoadRenderTexture(screenWidth, screenHeight);
    
    //GenTextureMipmaps(checkered_pattern.texture);
    //GenTextureMipmaps(&textures[TEX_PLAYER]);
    
    SetTextureFilter(*checkered_pattern.texture, TEXTURE_FILTER_BILINEAR);
    //SetTextureFilter(textures[TEX_PLAYER], TEXTURE_FILTER_BILINEAR);
    
    Shader shader = LoadShader(0, TextFormat("C:/work/DangerBall/assets/shader.glsl", GLSL_VERSION));
    
    global_pixel_shader = &shader;
    
    Match_Settings test_match_settings = {0};
    
    int intro_animation_played;
    float match_time;
    
    MatchInit(textures, &intro_animation_played, &match_time, &test_match, &test_match_settings);
    
    
    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        float dt = GetFrameTime();
        
        if(!intro_animation_played)
        {
            MatchStartAnimationPlay(&test_match_settings);
            intro_animation_played = 1;
        }
        
        if(!match_over)
        {
            //temporary
            if(IsKeyDown(KEY_Q)){ktmul += 3.0f * dt;}
            if(IsKeyDown(KEY_A)){ktmul -= 3.0f * dt;}
            if(IsKeyDown(KEY_W)){dkmin += 100.0f * dt;}
            if(IsKeyDown(KEY_S)){dkmin -= 100.0f * dt;}
            if(IsKeyDown(KEY_E)){dkmax += 100.0f * dt;}
            if(IsKeyDown(KEY_D)){dkmax -= 100.0f * dt;}
            if(IsKeyDown(KEY_R)){dkfinal += 100.0f * dt;}
            if(IsKeyDown(KEY_F)){dkfinal -= 100.0f * dt;}
            
            
            PlayerCollisionsUpdate(dt);
            
            PlayerStateUpdate(&player1, dt);
            PlayerStateUpdate(&player2, dt);
            
            for(int i = 0; i < test_match.number_of_items; i++)
            {
                if(test_match.items[i].active && CircleCollisionDetect(player1.pos, 40.0f, test_match.items[i].pos, 25.0f))
                {
                    player1.item = test_match.items[i].item;
                    test_match.items[i].active = 0;
                }
                else if(test_match.items[i].active && CircleCollisionDetect(player2.pos, 40.0f, test_match.items[i].pos, 25.0f))
                {
                    player2.item = test_match.items[i].item;
                    test_match.items[i].active = 0;
                }
                else
                {
                    test_match.items[i].animation += dt;
                }
                
            }
            
            PlayerPositionUpdate(&player1, dt, (Vector2) {-checkered_pattern.texture->width * 10.0f, -checkered_pattern.texture->height * 10.0f}, (Vector2) {checkered_pattern.texture->width * 10.0f, checkered_pattern.texture->height * 10.0f});
            PlayerPositionUpdate(&player2, dt, (Vector2) {-checkered_pattern.texture->width * 10.0f, -checkered_pattern.texture->height * 10.0f}, (Vector2) {checkered_pattern.texture->width * 10.0f, checkered_pattern.texture->height * 10.0f});
            
            PlayerBallCollisionResolve(&player1);
            PlayerBallCollisionResolve(&player2);
            
            BallUpdate(&g_ball, dt);
            
            match_time -= dt;
            if(match_time <= 0.0f) 
            {
                match_over = 1;
                MatchResultsAnimationPlay(&test_match_settings);
                match_over = 0;
                MatchInit(textures, &intro_animation_played, &match_time, &test_match, &test_match_settings); 
            }
        }
        
        
        
        int number_of_players = 2;
        
        Vector2 entities[3] = {player1.pos, player2.pos, g_ball.pos};
        
        CameraSetTargetAndZoom(&global_camera, entities, number_of_players + 1, dt);
        
        Vector2 distance_vector = Vector2Subtract(player1.pos, player2.pos);
        float distance = Vector2Length((Vector2) {distance_vector.x, distance_vector.y * 1.4f});
        
        global_camera.offset.x = screenWidth/2.0f;
        global_camera.offset.y = screenHeight/2.0f;
        
        CameraResolveBoundaries(&global_camera, (Vector2) {-checkered_pattern.texture->width * 10.0f, -checkered_pattern.texture->height * 10.0f}, (Vector2) {checkered_pattern.texture->width * 10.0f, checkered_pattern.texture->height * 10.0f}, (Rectangle) {0.0f, 0.0f, screenWidth, screenHeight});
        
        
        
        
        
        // Draw
        //----------------------------------------------------------------------------------
        
        //BeginTextureMode(target);
        
        
        //EndTextureMode();
        
        
        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        GameRender(&test_match);
        
        
        //DrawTextureRec(target.texture, (Rectangle){ 0, 0, (float)target.texture.width, (float)-target.texture.height }, (Vector2){ 0, 0 }, WHITE);
        if(!match_over)
        {
            DrawText(TextFormat("%.3f", match_time), 15, screenHeight - 80, 80, WHITE);
        }
        
        DrawText(TextFormat("FPS: %d", GetFPS()), 10, 10, 20, WHITE);
        
        EndDrawing();
        //----------------------------------------------------------------------------------
    }
    
    // De-Initialization
    //--------------------------------------------------------------------------------------
    
    
    CloseWindow();                // Close window and OpenGL context
    //--------------------------------------------------------------------------------------
    
    return 0;
}