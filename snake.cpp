#include <iostream>
#include <fstream>
#include <windows.h>   // 控制台光标、GetAsyncKeyState、GetModuleFileName
#include <conio.h>     // _kbhit() / _getch()
#include <deque>
#include <cstdlib>
#include <ctime>
#include <string>

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

void clearScreen() {
    system("cls");
}

// ---------- 最高分存取 ----------
// 获取 exe 所在目录(带末尾斜杠),让最高分文件跟 exe 放在一起
string getExeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    string path(buf);
    size_t pos = path.find_last_of("\\/");
    return path.substr(0, pos + 1);
}

int loadHighScore(const string& file) {
    ifstream in(file);
    int hs = 0;
    if (in) in >> hs;
    return hs;
}

void saveHighScore(const string& file, int hs) {
    ofstream out(file);
    if (out) out << hs;
}

// ---------- 主游戏 ----------
int main() {
    // 初始化随机数种子
    srand((unsigned)time(NULL));

    hideCursor();

    // 最高分文件放在 exe 同目录,跨局持久保存
    string highScoreFile = getExeDir() + "snake_highscore.txt";
    int highScore = loadHighScore(highScoreFile);

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
    bool paused = false;

    // 欢迎界面
    clearScreen();
    cout << "========================================\n";
    cout << "             贪吃蛇 (Snake)\n";
    cout << "========================================\n";
    cout << "  移动:  W/A/S/D 或 方向键\n";
    cout << "  暂停:  空格 或 P\n";
    cout << "  加速:  按住 Shift(随得分还会自动提速)\n";
    cout << "  退出:  ESC\n";
    cout << "  当前最高分: " << highScore << "\n";
    cout << "  按任意键开始...\n";
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
                    case ' ': case 'p': case 'P': paused = !paused; break; // 暂停/继续
                    case 27: gameOver = true; break; // ESC 退出
                }
            }
        }
        if (gameOver) break;

        // ---------- 状态栏(得分 / 最高分 / 长度 / 暂停 / 加速) ----------
        gotoXY(0, HEIGHT + 3);
        cout << "                                                            "; // 先清空整行
        gotoXY(0, HEIGHT + 3);
        cout << "得分:" << score << "  最高分:" << highScore << "  长度:" << snake.size();
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) cout << "  [加速]";
        if (paused) cout << "  [已暂停,按空格继续]";

        // 暂停时不推进游戏
        if (paused) { Sleep(50); continue; }

        // ---------- 禁止反向(不能直接掉头) ----------
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
        // 食物
        gotoXY(food.x + 1, food.y + 1);
        cout << "@";
        // 蛇头
        gotoXY(snake.front().x + 1, snake.front().y + 1);
        cout << "O";
        // 蛇身(从第二个格子开始)
        for (size_t i = 1; i < snake.size(); i++) {
            gotoXY(snake[i].x + 1, snake[i].y + 1);
            cout << "o";
        }

        // ---------- 速度:随得分自动提升,按住 Shift 再翻倍 ----------
        int baseDelay = 120 - (score / 10) * 10; // 每吃一个食物快 10ms
        if (baseDelay < 40) baseDelay = 40;       // 下限
        int delay = baseDelay;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) delay = baseDelay / 2; // Shift 加速

        Sleep(delay);
    }

    // ---------- 游戏结束:更新最高分 ----------
    bool newRecord = (score > highScore);
    if (newRecord) {
        highScore = score;
        saveHighScore(highScoreFile, highScore);
    }

    gotoXY(0, HEIGHT + 5);
    if (newRecord) cout << "*** 新纪录! ***" << endl;
    cout << "游戏结束! 最终得分: " << score << "   最高分: " << highScore << endl;
    cout << "按任意键退出..." << endl;
    _getch();

    return 0;
}
