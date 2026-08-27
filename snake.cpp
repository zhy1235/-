#include <iostream>
#include <windows.h>   // 控制台光标、隐藏光标
#include <conio.h>     // _kbhit() / _getch()
#include <deque>
#include <cstdlib>
#include <ctime>

using namespace std;

// ---------- 游戏参数 ----------
const int WIDTH  = 30;   // 游戏区宽度(格子数)
const int HEIGHT = 20;   // 游戏区高度(格子数)

// 方向
enum Direction { UP, DOWN, LEFT, RIGHT };

// 一个格子的坐标
struct Point {
    int x, y;
};

// ---------- 控制台辅助函数 ----------
void gotoXY(int x, int y) {
    COORD c;
    c.X = x;
    c.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

void hideCursor() {
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
}

// 清空控制台(可选:用于结束后的刷新)
void clearScreen() {
    system("cls");
}

// ---------- 主游戏 ----------
int main() {
    // 初始化随机数种子
    srand((unsigned)time(NULL));

    hideCursor();

    // 蛇的身体,用 deque 表示,front 是蛇头
    deque<Point> snake;
    Point head;
    head.x = WIDTH / 2;
    head.y = HEIGHT / 2;
    snake.push_back(head);

    // 初始方向:向右
    Direction dir = RIGHT;
    Direction nextDir = RIGHT;

    // 食物
    Point food;
    food.x = rand() % WIDTH;
    food.y = rand() % HEIGHT;

    int score = 0;
    bool gameOver = false;

    // 欢迎界面
    clearScreen();
    cout << "======================================\n";
    cout << "          贪吃蛇 (Snake)\n";
    cout << "======================================\n";
    cout << "  操作: W/A/S/D 或 方向键 控制移动\n";
    cout << "  按任意键开始游戏...\n";
    _getch();

    clearScreen();

    // 绘制游戏边框
    for (int y = 0; y <= HEIGHT + 1; y++) {
        for (int x = 0; x <= WIDTH + 1; x++) {
            if (y == 0 || y == HEIGHT + 1 || x == 0 || x == WIDTH + 1) {
                gotoXY(x, y);
                cout << "#";
            }
        }
    }

    // 游戏循环
    while (!gameOver) {
        // ---------- 处理输入 ----------
        while (_kbhit()) {
            int ch = _getch();
            // 方向键会先返回 224 或 0
            if (ch == 224 || ch == 0) {
                ch = _getch();
                switch (ch) {
                    case 72: nextDir = UP;    break;
                    case 80: nextDir = DOWN;  break;
                    case 75: nextDir = LEFT;  break;
                    case 77: nextDir = RIGHT; break;
                }
            } else {
                switch (ch) {
                    case 'w': case 'W': nextDir = UP;    break;
                    case 's': case 'S': nextDir = DOWN;  break;
                    case 'a': case 'A': nextDir = LEFT;  break;
                    case 'd': case 'D': nextDir = RIGHT; break;
                    case 27: gameOver = true; break; // ESC 退出
                }
            }
        }

        // 禁止反向(不能直接掉头)
        if (!(dir == UP    && nextDir == DOWN)  &&
            !(dir == DOWN  && nextDir == UP)    &&
            !(dir == LEFT  && nextDir == RIGHT) &&
            !(dir == RIGHT && nextDir == LEFT)) {
            dir = nextDir;
        }

        // ---------- 计算新蛇头 ----------
        Point newHead = snake.front();
        switch (dir) {
            case UP:    newHead.y--; break;
            case DOWN:  newHead.y++; break;
            case LEFT:  newHead.x--; break;
            case RIGHT: newHead.x++; break;
        }

        // ---------- 碰撞检测(墙壁) ----------
        if (newHead.x < 0 || newHead.x >= WIDTH ||
            newHead.y < 0 || newHead.y >= HEIGHT) {
            gameOver = true;
            break;
        }

        // ---------- 碰撞检测(自身) ----------
        for (size_t i = 0; i < snake.size(); i++) {
            if (snake[i].x == newHead.x && snake[i].y == newHead.y) {
                gameOver = true;
                break;
            }
        }
        if (gameOver) break;

        // ---------- 移动蛇 ----------
        snake.push_front(newHead);

        // 判断是否吃到食物
        if (newHead.x == food.x && newHead.y == food.y) {
            score += 10;
            // 生成新食物,避免生成在蛇身上
            bool onSnake;
            do {
                onSnake = false;
                food.x = rand() % WIDTH;
                food.y = rand() % HEIGHT;
                for (size_t i = 0; i < snake.size(); i++) {
                    if (snake[i].x == food.x && snake[i].y == food.y) {
                        onSnake = true;
                        break;
                    }
                }
            } while (onSnake);
        } else {
            // 没吃到食物,移除尾巴
            snake.pop_back();
        }

        // ---------- 绘制 ----------
        // 绘制食物
        gotoXY(food.x + 1, food.y + 1);
        cout << "@";

        // 绘制蛇头
        gotoXY(snake.front().x + 1, snake.front().y + 1);
        cout << "O";

        // 绘制蛇身(从第二个格子开始)
        for (size_t i = 1; i < snake.size(); i++) {
            gotoXY(snake[i].x + 1, snake[i].y + 1);
            cout << "o";
        }

        // 分数显示
        gotoXY(0, HEIGHT + 3);
        cout << "得分: " << score << "   长度: " << snake.size() << "   ";

        // 帧率控制
        Sleep(100);
    }

    // ---------- 游戏结束 ----------
    gotoXY(0, HEIGHT + 5);
    cout << "游戏结束! 最终得分: " << score << endl;
    cout << "按任意键退出..." << endl;
    _getch();

    return 0;
}
