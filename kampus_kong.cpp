#include <ncurses.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

using namespace std;

// ---------------- Konstanta ----------------
const int NUM_LANTAI = 5;
const int LANTAI[NUM_LANTAI] = {21, 17, 13, 9, 5};      // baris tempat '=' digambar (bawah -> atas)
const int TANGGA_X[NUM_LANTAI - 1] = {50, 10, 50, 20};  // posisi x tangga antar lantai
const int KIRI = 2;
const int KANAN = 57;
const int FRAME_MS = 50;
const int DURASI_LOMPAT = 12;   // dalam frame
const int DURASI_KOPI = 120;    // dalam frame (6 detik)
const int JEDA_GERAK = 3;       // pemain bergerak tiap 3 frame
const int NYAWA_AWAL = 3;       // "SKS"
const int SPAWN_X = 12;         // posisi tugas muncul (dari tangan Dosen Kong)
const char *FILE_HIGHSCORE = "highscore.txt";

enum Warna { W_PEMAIN = 1, W_TUGAS, W_LANTAI, W_TANGGA, W_BOSS, W_KOPI, W_TUJUAN };

// ---------------- Struktur data ----------------
struct Pemain {
    int x, y;       // y = baris tempat berdiri
    int lompat;     // sisa frame lompat (0 = di tanah)
    int cooldown;   // jeda antar gerakan
    int kebal;      // sisa frame efek kopi
};

struct Tugas {
    int x, y, arah;
    bool jatuh;
    char tipe;        // L = Laporan, M = Makalah, K = Kuis, D = Deadline
    int kecepatan;    // jumlah frame per langkah (makin kecil makin cepat)
    int hitung;
    bool sudahSkor;
    bool aktif;
};

struct Kopi {
    int x, y;
    bool aktif;
};

struct Game {
    Pemain p;
    vector<Tugas> tugas;
    vector<Kopi> kopi;
    int skor, level, nyawa, highscore, frame, spawnHitung;
};

// ---------------- File high score ----------------
int bacaHighScore() {
    ifstream f(FILE_HIGHSCORE);
    int h = 0;
    if (f >> h) return h;
    return 0;
}

void simpanHighScore(int h) {
    ofstream f(FILE_HIGHSCORE);
    f << h;
}

// ---------------- Fungsi bantu ----------------
void tengah(int baris, const char *teks) {
    mvaddstr(baris, (COLS - (int)strlen(teks)) / 2, teks);
}

bool diLantai(int y) {
    for (int i = 0; i < NUM_LANTAI; i++)
        if (y == LANTAI[i] - 1) return true;
    return false;
}

bool bisaNaik(const Pemain &p) {
    for (int s = 0; s < NUM_LANTAI - 1; s++)
        if (p.x == TANGGA_X[s] && p.y <= LANTAI[s] - 1 && p.y >= LANTAI[s + 1]) return true;
    return false;
}

bool bisaTurun(const Pemain &p) {
    for (int s = 0; s < NUM_LANTAI - 1; s++)
        if (p.x == TANGGA_X[s] && p.y >= LANTAI[s + 1] - 1 && p.y <= LANTAI[s] - 2) return true;
    return false;
}

// ---------------- Setup ----------------
void mulaiLevel(Game &g) {
    g.p = Pemain{4, LANTAI[0] - 1, 0, 0, 0};
    g.tugas.clear();
    g.kopi.clear();
    g.kopi.push_back(Kopi{10 + rand() % 35, LANTAI[1] - 1, true});
    g.kopi.push_back(Kopi{25 + rand() % 25, LANTAI[3] - 1, true});
    g.spawnHitung = 0;
}

void resetGame(Game &g) {
    g.skor = 0;
    g.level = 1;
    g.nyawa = NYAWA_AWAL;
    g.frame = 0;
    mulaiLevel(g);
}

// ---------------- Logika game ----------------
void prosesInput(Game &g, int ch) {
    Pemain &p = g.p;

    if (ch == ' ' && p.lompat == 0 && diLantai(p.y)) {
        p.lompat = DURASI_LOMPAT;
        return;
    }
    if (p.cooldown > 0) return;

    bool gerak = false;
    switch (ch) {
        case KEY_LEFT: case 'a': case 'A':
            if (diLantai(p.y) && p.x > KIRI) { p.x--; gerak = true; }
            break;
        case KEY_RIGHT: case 'd': case 'D':
            if (diLantai(p.y) && p.x < KANAN) { p.x++; gerak = true; }
            break;
        case KEY_UP: case 'w': case 'W':
            if (p.lompat == 0 && bisaNaik(p)) { p.y--; gerak = true; }
            break;
        case KEY_DOWN: case 's': case 'S':
            if (p.lompat == 0 && bisaTurun(p)) { p.y++; gerak = true; }
            break;
    }
    if (gerak) p.cooldown = JEDA_GERAK;
}

void munculkanTugas(Game &g) {
    const char jenis[] = {'L', 'M', 'K', 'D'};
    int dasar = max(3, 6 - g.level);
    int kec[] = {dasar + 1, dasar, max(2, dasar - 1), max(2, dasar - 2)};
    int i = rand() % 4;
    g.tugas.push_back(Tugas{SPAWN_X, LANTAI[NUM_LANTAI - 1] - 1, +1, false,
                            jenis[i], kec[i], 0, false, true});
}

void gerakkanTugas(Tugas &t) {
    if (t.jatuh) {
        t.y++;
        if (diLantai(t.y)) {          // mendarat di lantai berikutnya
            t.jatuh = false;
            t.arah = -t.arah;
        }
        return;
    }
    bool diUjung = (t.arah > 0 && t.x >= KANAN) || (t.arah < 0 && t.x <= KIRI);
    if (diUjung) {
        if (t.y == LANTAI[0] - 1) t.aktif = false;   // sudah di lantai dasar -> hilang
        else { t.jatuh = true; t.y++; }
    } else {
        t.x += t.arah;
    }
}

void updateTugas(Game &g) {
    int jeda = max(18, 45 - g.level * 5);
    if (++g.spawnHitung >= jeda) {
        g.spawnHitung = 0;
        munculkanTugas(g);
    }
    for (size_t i = 0; i < g.tugas.size(); i++) {
        Tugas &t = g.tugas[i];
        if (++t.hitung >= t.kecepatan) {
            t.hitung = 0;
            gerakkanTugas(t);
        }
    }
    g.tugas.erase(remove_if(g.tugas.begin(), g.tugas.end(),
                            [](const Tugas &t) { return !t.aktif; }),
                  g.tugas.end());
}

// true bila pemain terkena tugas
bool cekTabrakan(Game &g) {
    Pemain &p = g.p;
    bool kena = false;
    for (size_t i = 0; i < g.tugas.size(); i++) {
        Tugas &t = g.tugas[i];
        if (!t.aktif || t.x != p.x || t.y != p.y) continue;
        if (p.lompat > 0) {                       // melompati tugas
            if (!t.sudahSkor) { g.skor += 100; t.sudahSkor = true; }
        } else if (p.kebal > 0) {                 // efek kopi: tugas hancur
            t.aktif = false;
            g.skor += 200;
        } else {
            kena = true;
        }
    }
    return kena;
}

void cekKopi(Game &g) {
    for (size_t i = 0; i < g.kopi.size(); i++) {
        Kopi &k = g.kopi[i];
        if (k.aktif && k.x == g.p.x && k.y == g.p.y) {
            k.aktif = false;
            g.p.kebal = DURASI_KOPI;
            g.skor += 50;
        }
    }
}

// ---------------- Tampilan ----------------
void gambar(const Game &g) {
    erase();

    mvprintw(0, 0, "SKOR:%05d  HI:%05d  LEVEL:%d  SKS:%d%s",
             g.skor, g.highscore, g.level, g.nyawa, g.p.kebal > 0 ? "  [KOPI!]" : "");

    attron(COLOR_PAIR(W_LANTAI));
    for (int i = 0; i < NUM_LANTAI; i++)
        for (int x = KIRI - 1; x <= KANAN + 1; x++) mvaddch(LANTAI[i], x, '=');
    attroff(COLOR_PAIR(W_LANTAI));

    attron(COLOR_PAIR(W_TANGGA));
    for (int s = 0; s < NUM_LANTAI - 1; s++)
        for (int y = LANTAI[s + 1]; y <= LANTAI[s] - 1; y++) mvaddch(y, TANGGA_X[s], 'H');
    attroff(COLOR_PAIR(W_TANGGA));

    attron(COLOR_PAIR(W_BOSS) | A_BOLD);
    mvaddstr(1, 1, "DOSEN KONG");
    mvaddstr(2, 1, " \\(o.o)/ ");
    mvaddstr(3, 1, "  [KONG] ");
    mvaddstr(4, 1, "  /   \\  ");
    attroff(COLOR_PAIR(W_BOSS) | A_BOLD);

    attron(COLOR_PAIR(W_TUJUAN) | A_BOLD);
    mvaddstr(3, 54, "LULUS!");
    mvaddstr(4, 55, "(A)");
    attroff(COLOR_PAIR(W_TUJUAN) | A_BOLD);

    attron(COLOR_PAIR(W_KOPI) | A_BOLD);
    for (size_t i = 0; i < g.kopi.size(); i++)
        if (g.kopi[i].aktif) mvaddch(g.kopi[i].y, g.kopi[i].x, 'C');
    attroff(COLOR_PAIR(W_KOPI) | A_BOLD);

    attron(COLOR_PAIR(W_TUGAS) | A_BOLD);
    for (size_t i = 0; i < g.tugas.size(); i++)
        mvaddch(g.tugas[i].y, g.tugas[i].x, g.tugas[i].tipe);
    attroff(COLOR_PAIR(W_TUGAS) | A_BOLD);

    int py = g.p.y - (g.p.lompat > 0 ? 1 : 0);
    attron(COLOR_PAIR(W_PEMAIN) | A_BOLD);
    if (g.p.kebal > 0 && (g.frame / 3) % 2 == 0) attron(A_REVERSE);
    mvaddch(py, g.p.x, '@');
    attroff(A_REVERSE);
    attroff(COLOR_PAIR(W_PEMAIN) | A_BOLD);

    mvaddstr(23, 0, "Panah: gerak/tangga  Spasi: lompat  P: pause  Q: keluar");
    refresh();
}

// ---------------- Layar-layar ----------------
void layarInstruksi() {
    nodelay(stdscr, FALSE);
    erase();
    attron(A_BOLD);
    tengah(2, "== CARA BERMAIN ==");
    attroff(A_BOLD);
    tengah(4,  "Kamu Maba Nana (@). Panjat gedung kampus sampai");
    tengah(5,  "puncak untuk meraih Nilai A, hindari tugas dari Dosen Kong!");
    tengah(7,  "Panah Kiri/Kanan : jalan     Panah Atas/Bawah : tangga (H)");
    tengah(8,  "Spasi : lompat               P : pause     Q : keluar");
    tengah(10, "Rintangan (tugas):");
    tengah(11, "L = Laporan (lambat)       M = Makalah (sedang)");
    tengah(12, "K = Kuis dadakan (cepat)   D = Deadline (paling cepat)");
    tengah(14, "C = Kopi : kebal sementara, tabrak tugas = hancur (+200)");
    tengah(15, "Lompati tugas = +100 poin, sampai puncak = +500 poin");
    tengah(16, "Nyawa disebut SKS. Habis SKS = game over.");
    tengah(19, "Tekan tombol apa saja untuk kembali");
    refresh();
    getch();
}

// mengembalikan 'm' (mulai) atau 'q' (keluar)
int menuUtama(int highscore) {
    nodelay(stdscr, FALSE);
    while (true) {
        erase();
        attron(COLOR_PAIR(W_BOSS) | A_BOLD);
        tengah(4, "=============================");
        tengah(5, "   K A M P U S   K O N G     ");
        tengah(6, "=============================");
        attroff(COLOR_PAIR(W_BOSS) | A_BOLD);
        tengah(9, "Panjat sampai puncak, hindari tugas!");
        mvprintw(11, (COLS - 18) / 2, "High Score : %05d", highscore);
        tengah(14, "[ENTER] Mulai   [I] Instruksi   [Q] Keluar");
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) return 'm';
        if (ch == 'i' || ch == 'I') layarInstruksi();
        if (ch == 'q' || ch == 'Q') return 'q';
    }
}

// true = main lagi, false = keluar
bool layarGameOver(const Game &g) {
    nodelay(stdscr, FALSE);
    erase();
    attron(COLOR_PAIR(W_TUGAS) | A_BOLD);
    tengah(7, "=== GAME OVER ===");
    tengah(8, "SKS habis, kamu harus mengulang semester!");
    attroff(COLOR_PAIR(W_TUGAS) | A_BOLD);
    mvprintw(10, (COLS - 18) / 2, "Skor       : %05d", g.skor);
    mvprintw(11, (COLS - 18) / 2, "High Score : %05d", g.highscore);
    tengah(14, "[R] Main lagi   [Q] Keluar");
    refresh();
    while (true) {
        int ch = getch();
        if (ch == 'r' || ch == 'R') return true;
        if (ch == 'q' || ch == 'Q') return false;
    }
}

void pesan(const char *teks) {
    attron(A_REVERSE | A_BOLD);
    tengah(11, teks);
    attroff(A_REVERSE | A_BOLD);
    refresh();
    napms(1200);
}

// ---------------- Game loop ----------------
// mengembalikan true bila pemain menekan Q (keluar langsung)
bool mainkan(Game &g) {
    nodelay(stdscr, TRUE);
    while (true) {
        int ch = getch();
        flushinp();

        if (ch == 'q' || ch == 'Q') return true;
        if (ch == 'p' || ch == 'P') {
            tengah(11, " == PAUSE (tekan P) == ");
            refresh();
            nodelay(stdscr, FALSE);
            int c;
            do { c = getch(); } while (c != 'p' && c != 'P' && c != 'q' && c != 'Q');
            nodelay(stdscr, TRUE);
            if (c == 'q' || c == 'Q') return true;
            continue;
        }

        prosesInput(g, ch);
        bool kena = cekTabrakan(g);
        cekKopi(g);

        // sampai puncak
        if (g.p.y == LANTAI[NUM_LANTAI - 1] - 1 && g.p.x >= 55) {
            g.skor += 500;
            g.level++;
            gambar(g);
            pesan(" NILAI A! LEVEL NAIK ");
            mulaiLevel(g);
            continue;
        }

        if (!kena) {
            updateTugas(g);
            kena = cekTabrakan(g);
        }

        if (kena) {
            g.nyawa--;
            gambar(g);
            pesan(" KENA TUGAS! SKS BERKURANG ");
            if (g.nyawa <= 0) return false;
            mulaiLevel(g);
            continue;
        }

        if (g.p.lompat > 0) g.p.lompat--;
        if (g.p.cooldown > 0) g.p.cooldown--;
        if (g.p.kebal > 0) g.p.kebal--;
        g.frame++;

        gambar(g);
        napms(FRAME_MS);
    }
}

// ---------------- main ----------------
int main() {
    srand((unsigned)time(NULL));
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (LINES < 24 || COLS < 60) {
        endwin();
        printf("Terminal terlalu kecil. Minimal 60 kolom x 24 baris (sekarang %d x %d).\n", COLS, LINES);
        return 1;
    }

    if (has_colors()) {
        start_color();
        init_pair(W_PEMAIN, COLOR_CYAN, COLOR_BLACK);
        init_pair(W_TUGAS, COLOR_RED, COLOR_BLACK);
        init_pair(W_LANTAI, COLOR_YELLOW, COLOR_BLACK);
        init_pair(W_TANGGA, COLOR_WHITE, COLOR_BLACK);
        init_pair(W_BOSS, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(W_KOPI, COLOR_GREEN, COLOR_BLACK);
        init_pair(W_TUJUAN, COLOR_GREEN, COLOR_BLACK);
    }

    Game g = Game();
    g.highscore = bacaHighScore();

    while (true) {
        if (menuUtama(g.highscore) == 'q') break;

        resetGame(g);
        bool keluar = mainkan(g);

        if (g.skor > g.highscore) {
            g.highscore = g.skor;
            simpanHighScore(g.highscore);
        }
        if (keluar) break;
        if (!layarGameOver(g)) break;
    }

    endwin();
    return 0;
}
