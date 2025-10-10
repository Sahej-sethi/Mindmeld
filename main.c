#include "raylib.h"
#include <stdlib.h> // Required for: rand(), srand()
#include <time.h>   // Required for: time()

int main(void)
{
    // 1. Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "Hackathon Game!");
    SetTargetFPS(60);

    srand(time(NULL));

    // 2. Game Variables
    //--------------------------------------------------------------------------------------
    float speed = 5.0f;
    Rectangle player = { (float)screenWidth / 2 - 50, (float)screenHeight - 40, 100.0f, 20.0f };
    Vector2 ball = { (float)screenWidth / 2, 15.0f };
    float radius = 15.0f;
    float ballspeedx = GetRandomValue(-2.0f,2.0f);
    float ballspeedy = 5.0f;
    int score = 0;
    bool gameover = false;

    // 3. Main Game Loop
    //--------------------------------------------------------------------------------------
    while (!WindowShouldClose())
    {
        // ==================================================
        // UPDATE SECTION - All logic happens here
        // ==================================================
        if (!gameover)
        {
            if (IsKeyDown(KEY_RIGHT)) {
                player.x += speed;
            }
            if (IsKeyDown(KEY_LEFT)) {
                player.x -= speed;
            }

            
            if (player.x < 0) {
                player.x = 0.0f;
            }
            if (player.x + player.width > screenWidth) {
                player.x = screenWidth - player.width;
            }

            
             ball.x += ballspeedx;
             ball.y += ballspeedy;

            if(ball.x+radius>=screenWidth || ball.x-radius<=0){
                ballspeedx *= -1.0f;
                ballspeedx += (ballspeedx)*(0.01);
                ballspeedy += (ballspeedy)*(0.01);
                speed += (speed)*(0.01);
            }
            if(ball.y-radius<=0){
                ballspeedy *= -1.0f;
                ballspeedx += (ballspeedx)*(0.01);
                ballspeedy += (ballspeedy)*(0.01);
                speed += (speed)*(0.01);
            }
            if (CheckCollisionCircleRec(ball, radius, player))
            {  
                score += 10;
                ballspeedy *= -1.0f;
                ballspeedx += (ballspeedx)*(0.01);
                ballspeedy += (ballspeedy)*(0.01);
                speed += (speed)*(0.01);
            }
            else 
            {
               
                if (ball.y > screenHeight)
                {
                    gameover = true;
                }
            }
        }else{
            if(IsKeyPressed(KEY_ENTER)){
                gameover = false;
                score = 0;
                player.x = (float)screenWidth / 2 - 50;
                ball.x = GetRandomValue(radius,screenWidth-radius);
                ball.y = 15.0f;
                ballspeedx = GetRandomValue(-2.0f,2.0f);
                ballspeedy = 5.0f;
            }
        }
        
        BeginDrawing();
            ClearBackground(WHITE);

            if (!gameover) {
                DrawRectangleRec(player, BLUE);
                DrawCircleV(ball, radius, GREEN);
                DrawText(TextFormat("Score: %d", score), 10, 10, 20, BLACK);
            }
            else {

                DrawText("Game Over", screenWidth / 2 - MeasureText("Game Over", 50) / 2, screenHeight / 2 - 50, 50, RED);
                DrawText("You Lose!", screenWidth / 2 - MeasureText("You Lose!", 30) / 2, screenHeight / 2 + 10, 30, LIGHTGRAY);
            }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}