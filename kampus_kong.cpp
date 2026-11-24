#include <ncurses/ncurses.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

using namespace std;

// ---------------- Konstanta ----------------
const int NUM_LEVEL  = 5;       // jumlah level = jumlah peta
const int MAX_LANTAI = 5;       // kapasitas lantai per peta
const int KIRI = 2;
const int KANAN = 57;
const int FRAME_MS = 50;
const int DURASI_LOMPAT = 7;    // dalam frame (~0.35 detik)
const int DURASI_KOPI = 120;    // dalam frame (6 detik kebal rintangan)
const int DURASI_BEKU = 80;     // dalam frame (4 detik tugas berhenti)
const int JEDA_GERAK = 3;       // pemain bergerak tiap 3 frame
const int NYAWA_AWAL = 3;       // "SKS" awal
const int MAX_NYAWA = 5;        // batas maksimal "SKS"
const char *FILE_HIGHSCORE = "highscore.txt";

enum Warna {
    W_PEMAIN = 1,
    W_TUGAS,
    W_LANTAI,
    W_TANGGA,
    W_BOSS,
    W_KOPI,
    W_TUJUAN,
    W_NYAWA,
    W_BEKU
};

enum TipePowerUp {
    PU_KEBAL,   // 'C' = Kopi (kebal & hancurkan tugas)
    PU_NYAWA,   // '+' = Tambah 1 SKS (nyawa)
    PU_BEKU     // 'B' = Bekukan pergerakan tugas sementara
};

enum HasilMain { HASIL_KELUAR, HASIL_GAMEOVER, HASIL_MENANG };

// ---------------- Struktur data ----------------
struct Pemain {
    int x, y;       // y = baris tempat berdiri
    int lompat;     // sisa frame lompat (0 = di tanah)
    int cooldown;   // jeda antar gerakan
    int kebal;      // sisa frame efek kebal (kopi)
    int beku;       // sisa frame efek beku tugas
};

struct Tugas {
    int x, y, arah;
    bool jatuh;
    char tipe;        // L = Laporan, M = Makalah, K = Kuis, D = Deadline
    int kecepatan;    // jumlah frame per langkah (makin besar makin lambat)
    int hitung;
    bool sudahSkor;
    bool aktif;
};

struct PowerUp {
    int x, y;
    TipePowerUp tipe;
    bool aktif;
};

// ---------------- Peta: satu per level ----------------
struct Map {
    const char *nama;
    int jumlahLantai;                 // 4 atau 5
    int lantai[MAX_LANTAI];           // baris '=' dari bawah ke atas (0 = tidak dipakai)
    int tanggaX[MAX_LANTAI - 1];      // kolom 'H' penghubung lantai s -> s+1
    int pemainX;                      // posisi awal pemain di lantai dasar
    int bossX;                        // kolom kiri gambar Dosen Kong (di atas puncak)
    int arahAwal;                     // +1: tugas dilempar ke kanan, -1: ke kiri
    int tujuanX;                      // kolom Nilai A di lantai puncak
};

const Map PETA[NUM_LEVEL] = {
    // LEVEL 1: peta asli
    {"GEDUNG KLASIK",  5, {21,17,13,9,5},  {50,10,50,20},  4,  1, +1, 55},
    // LEVEL 2: tangga bolak-balik ujung kiri <-> kanan
    {"TANGGA ZIG-ZAG", 5, {21,17,13,9,5},  {10,50,10,50}, 30,  1, +1, 55},
    // LEVEL 3: hanya 4 lantai, Dosen Kong pindah ke kanan, tujuan di kiri
    {"MENARA PENDEK",  4, {21,16,11,6,0},  {30,15,40,0},  55, 47, -1,  4},
    // LEVEL 4: jarak antar lantai tidak beraturan
    {"GEDUNG RAPAT",   5, {21,17,14,11,7}, {45,15,40,20}, 30,  1, +1, 55},
    // LEVEL 5: semua tangga berkumpul di tengah gedung
    {"LORONG TENGAH",  5, {21,17,13,9,5},  {30,28,32,30},  4, 47, -1,  4},
};

struct Game {
    Pemain p;
    vector<Tugas> tugas;
    vector<PowerUp> powerup;
    const Map *map;                   // peta level yang sedang dimainkan
    int skor, level, nyawa, highscore, frame;
    int spawnHitung, jedaSpawn, puSpawnHitung;
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

int spawnX(const Map &m) {
    return m.arahAwal > 0 ? m.bossX + 11 : m.bossX - 2;
}

int barisPuncak(const Map &m) {
    return m.lantai[m.jumlahLantai - 1];
}

bool diLantai(const Map &m, int y) {
    for (int i = 0; i < m.jumlahLantai; i++)
        if (y == m.lantai[i] - 1) return true;
    return false;
}

bool bisaNaik(const Map &m, const Pemain &p) {
    for (int s = 0; s < m.jumlahLantai - 1; s++)
        if (p.x == m.tanggaX[s] && p.y <= m.lantai[s] - 1 && p.y >= m.lantai[s + 1]) return true;
    return false;
}

bool bisaTurun(const Map &m, const Pemain &p) {
    for (int s = 0; s < m.jumlahLantai - 1; s++)
        if (p.x == m.tanggaX[s] && p.y >= m.lantai[s + 1] - 1 && p.y <= m.lantai[s] - 2) return true;
    return false;
}

void munculkanPowerUp(Game &g, int lantaiIdx = -1) {
    const Map &m = *g.map;
    if (lantaiIdx < 0) {
        lantaiIdx = 1 + rand() % (m.jumlahLantai - 2);
    }
    int px = KIRI + 3 + rand() % (KANAN - KIRI - 6);
    int py = m.lantai[lantaiIdx] - 1;

    if (px == g.p.x && py == g.p.y) {
        px = (px < KANAN - 4) ? px + 3 : px - 3;
    }

    int acak = rand() % 10;
    TipePowerUp tipe;
    if (acak < 4) tipe = PU_KEBAL;       // 40% peluang Kopi Kebal
    else if (acak < 7) tipe = PU_NYAWA;  // 30% peluang Tambah SKS
    else tipe = PU_BEKU;                 // 30% peluang Beku Tugas

    g.powerup.push_back(PowerUp{px, py, tipe, true});
}

// ---------------- Setup ----------------
void mulaiLevel(Game &g) {
    g.map = &PETA[min(g.level, NUM_LEVEL) - 1];   // <-- pilih peta sesuai level
    const Map &m = *g.map;

    g.p = Pemain{m.pemainX, m.lantai[0] - 1, 0, 0, 0, 0};
    g.tugas.clear();
    g.powerup.clear();

    munculkanPowerUp(g, 1);
    munculkanPowerUp(g, m.jumlahLantai - 2);

    g.spawnHitung = 0;
    g.jedaSpawn = 30;      // tugas pertama muncul setelah 1,5 detik
    g.puSpawnHitung = 0;
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
    const Map &m = *g.map;
    Pemain &p = g.p;

    if (ch == ' ' && p.lompat == 0 && diLantai(m, p.y)) {
        p.lompat = DURASI_LOMPAT;
        return;
    }
    if (p.cooldown > 0) return;

    bool gerak = false;
    switch (ch) {
        case KEY_LEFT: case 'a': case 'A':
            if (diLantai(m, p.y) && p.x > KIRI) { p.x--; gerak = true; }
            break;
        case KEY_RIGHT: case 'd': case 'D':
            if (diLantai(m, p.y) && p.x < KANAN) { p.x++; gerak = true; }
            break;
        case KEY_UP: case 'w': case 'W':
            if (p.lompat == 0 && bisaNaik(m, p)) { p.y--; gerak = true; }
            break;
        case KEY_DOWN: case 's': case 'S':
            if (p.lompat == 0 && bisaTurun(m, p)) { p.y++; gerak = true; }
            break;
    }
    if (gerak) p.cooldown = JEDA_GERAK;
}

void munculkanTugas(Game &g) {
    const Map &m = *g.map;
    const char jenis[] = {'L', 'M', 'K', 'D'};
    // Nilai dasar lebih besar agar kecepatan gerak tugas lebih santai
    int dasar = max(5, 9 - g.level);
    int kec[] = {dasar + 2, dasar + 1, dasar, max(4, dasar - 1)};
    int i = rand() % 4;
    g.tugas.push_back(Tugas{spawnX(m), barisPuncak(m) - 1, m.arahAwal, false,
                            jenis[i], kec[i], 0, false, true});
}

void gerakkanTugas(const Map &m, Tugas &t) {
    if (t.jatuh) {
        t.y++;
        if (diLantai(m, t.y)) {        // mendarat di lantai berikutnya
            t.jatuh = false;
            t.arah = -t.arah;
        }
        return;
    }
    bool diUjung = (t.arah > 0 && t.x >= KANAN) || (t.arah < 0 && t.x <= KIRI);
    if (diUjung) {
        if (t.y == m.lantai[0] - 1) t.aktif = false;   // sudah di lantai dasar -> hilang
        else { t.jatuh = true; t.y++; }
    } else {
        t.x += t.arah;
    }
}

void updateTugas(Game &g) {
    const Map &m = *g.map;
    // Jika efek beku aktif, tugas diam di tempat
    if (g.p.beku > 0) return;

    const int MAX_TUGAS = min(7, 4 + g.level); // batasi jumlah tugas bersamaan

    if (++g.spawnHitung >= g.jedaSpawn) {
        // Pastikan tidak ada tugas lain yang masih terlalu dekat dengan Dosen Kong
        bool terlaluDekat = false;
        int lantaiAtas = barisPuncak(m) - 1;
        for (size_t i = 0; i < g.tugas.size(); i++) {
            if (g.tugas[i].aktif && g.tugas[i].y == lantaiAtas &&
                abs(g.tugas[i].x - spawnX(m)) < 14) {
                terlaluDekat = true;
                break;
            }
        }

        if ((int)g.tugas.size() < MAX_TUGAS && !terlaluDekat) {
            munculkanTugas(g);
            g.spawnHitung = 0;
            int minJeda = max(40, 65 - g.level * 4);
            int maxJeda = max(60, 95 - g.level * 4);
            g.jedaSpawn = minJeda + rand() % (maxJeda - minJeda + 1);
        } else {
            g.spawnHitung = g.jedaSpawn - 5;
        }
    }

    for (size_t i = 0; i < g.tugas.size(); i++) {
        Tugas &t = g.tugas[i];
        if (++t.hitung >= t.kecepatan) {
            t.hitung = 0;
            gerakkanTugas(m, t);
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
        } else if (p.kebal > 0) {                 // efek kebal: tugas hancur
            t.aktif = false;
            g.skor += 200;
        } else {
            kena = true;
        }
    }
    return kena;
}

void cekPowerUp(Game &g) {
    for (size_t i = 0; i < g.powerup.size(); i++) {
        PowerUp &pu = g.powerup[i];
        if (!pu.aktif || pu.x != g.p.x || pu.y != g.p.y) continue;

        pu.aktif = false;
        switch (pu.tipe) {
            case PU_KEBAL:
                g.p.kebal = DURASI_KOPI;
                g.skor += 50;
                break;
            case PU_NYAWA:
                if (g.nyawa < MAX_NYAWA) {
                    g.nyawa++;
                    g.skor += 50;
                } else {
                    g.skor += 150;
                }
                break;
            case PU_BEKU:
                g.p.beku = DURASI_BEKU;
                g.skor += 50;
                break;
        }
    }

    g.powerup.erase(remove_if(g.powerup.begin(), g.powerup.end(),
                              [](const PowerUp &pu) { return !pu.aktif; }),
                    g.powerup.end());
}

// ---------------- Tampilan ----------------
void gambar(const Game &g) {
    const Map &m = *g.map;
    erase();

    mvprintw(0, 0, "SKOR:%05d  HI:%05d  LVL:%d/%d  SKS:%d/%d%s%s",
             g.skor, g.highscore, g.level, NUM_LEVEL, g.nyawa, MAX_NYAWA,
             g.p.kebal > 0 ? " [KEBAL]" : "",
             g.p.beku > 0 ? " [BEKU]" : "");

    attron(COLOR_PAIR(W_LANTAI));
    for (int i = 0; i < m.jumlahLantai; i++)
        for (int x = KIRI - 1; x <= KANAN + 1; x++) mvaddch(m.lantai[i], x, '=');
    attroff(COLOR_PAIR(W_LANTAI));

    attron(COLOR_PAIR(W_TANGGA));
    for (int s = 0; s < m.jumlahLantai - 1; s++)
        for (int y = m.lantai[s + 1]; y <= m.lantai[s] - 1; y++) mvaddch(y, m.tanggaX[s], 'H');
    attroff(COLOR_PAIR(W_TANGGA));

    int atas = barisPuncak(m);
    attron(COLOR_PAIR(W_BOSS) | A_BOLD);
    mvaddstr(atas - 4, m.bossX, "DOSEN KONG");
    mvaddstr(atas - 3, m.bossX, " \\(o.o)/ ");
    mvaddstr(atas - 2, m.bossX, "  [KONG] ");
    mvaddstr(atas - 1, m.bossX, "  /   \\  ");
    attroff(COLOR_PAIR(W_BOSS) | A_BOLD);

    attron(COLOR_PAIR(W_TUJUAN) | A_BOLD);
    mvaddstr(atas - 2, m.tujuanX - 1, "LULUS!");
    mvaddstr(atas - 1, m.tujuanX, "(A)");
    attroff(COLOR_PAIR(W_TUJUAN) | A_BOLD);

    for (size_t i = 0; i < g.powerup.size(); i++) {
        const PowerUp &pu = g.powerup[i];
        if (!pu.aktif) continue;
        if (pu.tipe == PU_KEBAL) {
            attron(COLOR_PAIR(W_KOPI) | A_BOLD);
            mvaddch(pu.y, pu.x, 'C');
            attroff(COLOR_PAIR(W_KOPI) | A_BOLD);
        } else if (pu.tipe == PU_NYAWA) {
            attron(COLOR_PAIR(W_NYAWA) | A_BOLD);
            mvaddch(pu.y, pu.x, '+');
            attroff(COLOR_PAIR(W_NYAWA) | A_BOLD);
        } else {
            attron(COLOR_PAIR(W_BEKU) | A_BOLD);
            mvaddch(pu.y, pu.x, 'B');
            attroff(COLOR_PAIR(W_BEKU) | A_BOLD);
        }
    }

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

    mvprintw(22, 0, "Peta %d: %s", g.level, m.nama);
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
    tengah(4,  "Kamu Maba Nana (@). Panjat 5 gedung kampus yang berbeda");
    tengah(5,  "sampai puncak untuk meraih Nilai A, hindari tugas Dosen Kong!");
    tengah(7,  "Panah Kiri/Kanan : jalan     Panah Atas/Bawah : tangga (H)");
    tengah(8,  "Spasi : lompat               P : pause     Q : keluar");
    tengah(10, "Rintangan (tugas):");
    tengah(11, "L = Laporan (lambat)       M = Makalah (sedang)");
    tengah(12, "K = Kuis dadakan (cepat)   D = Deadline (paling cepat)");
    tengah(14, "Power-Up:");
    tengah(15, "C = Kopi : kebal 6 detik & hancurkan tugas (+200 poin)");
    tengah(16, "+ = SKS Ekstra : tambah +1 nyawa (maks 5 SKS)");
    tengah(17, "B = Bebas Tugas : bekukan semua tugas selama 4 detik");
    tengah(19, "Lompati tugas = +100 poin, sampai puncak = +500 poin");
    tengah(20, "Tiap level petanya beda. Tamatkan level 5 untuk WISUDA!");
    tengah(22, "Tekan tombol apa saja untuk kembali");
    refresh();
    getch();
}

int menuUtama(int highscore) {
    nodelay(stdscr, FALSE);
    while (true) {
        erase();
        attron(COLOR_PAIR(W_BOSS) | A_BOLD);
        tengah(4, "=============================");
        tengah(5, "   K A M P U S   K O N G     ");
        tengah(6, "=============================");
        attroff(COLOR_PAIR(W_BOSS) | A_BOLD);
        tengah(9, "Panjat 5 gedung berbeda, hindari tugas!");
        mvprintw(11, (COLS - 18) / 2, "High Score : %05d", highscore);
        tengah(14, "[ENTER] Mulai   [I] Instruksi   [Q] Keluar");
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) return 'm';
        if (ch == 'i' || ch == 'I') layarInstruksi();
        if (ch == 'q' || ch == 'Q') return 'q';
    }
}

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

bool layarMenang(const Game &g) {
    nodelay(stdscr, FALSE);
    erase();
    attron(COLOR_PAIR(W_TUJUAN) | A_BOLD);
    tengah(6, "=== SELAMAT, KAMU WISUDA! ===");
    tengah(8, "5 gedung ditaklukkan, Nilai A di tangan!");
    attroff(COLOR_PAIR(W_TUJUAN) | A_BOLD);
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
HasilMain mainkan(Game &g) {
    nodelay(stdscr, TRUE);
    int levelTampil = 0;
    while (true) {
        const Map &m = *g.map;

        // tampilkan nama peta setiap ganti level
        if (levelTampil != g.level) {
            levelTampil = g.level;
            gambar(g);
            char buf[80];
            snprintf(buf, sizeof buf, " LEVEL %d: %s ", g.level, m.nama);
            pesan(buf);
        }

        int ch = getch();
        flushinp();

        if (ch == 'q' || ch == 'Q') return HASIL_KELUAR;
        if (ch == 'p' || ch == 'P') {
            tengah(11, " == PAUSE (tekan P) == ");
            refresh();
            nodelay(stdscr, FALSE);
            int c;
            do { c = getch(); } while (c != 'p' && c != 'P' && c != 'q' && c != 'Q');
            nodelay(stdscr, TRUE);
            if (c == 'q' || c == 'Q') return HASIL_KELUAR;
            continue;
        }

        prosesInput(g, ch);
        bool kena = cekTabrakan(g);
        cekPowerUp(g);

        // sampai puncak (zona Nilai A)
        if (g.p.y == barisPuncak(m) - 1 && abs(g.p.x - m.tujuanX) <= 2) {
            g.skor += 500;
            if (g.level >= NUM_LEVEL) {          // level 5 tamat -> menang
                gambar(g);
                pesan(" NILAI A! SEMUA GEDUNG SELESAI! ");
                return HASIL_MENANG;
            }
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
            if (g.nyawa <= 0) return HASIL_GAMEOVER;
            mulaiLevel(g);
            continue;
        }

        if (++g.puSpawnHitung >= 200) {
            g.puSpawnHitung = 0;
            if (g.powerup.size() < 3) {
                munculkanPowerUp(g);
            }
        }

        if (g.p.lompat > 0) g.p.lompat--;
        if (g.p.cooldown > 0) g.p.cooldown--;
        if (g.p.kebal > 0) g.p.kebal--;
        if (g.p.beku > 0) g.p.beku--;
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
        init_pair(W_NYAWA, COLOR_YELLOW, COLOR_BLACK);
        init_pair(W_BEKU, COLOR_CYAN, COLOR_BLACK);
    }

    Game g = Game();
    g.highscore = bacaHighScore();

    while (true) {
        if (menuUtama(g.highscore) == 'q') break;

        resetGame(g);
        HasilMain hasil = mainkan(g);

        if (g.skor > g.highscore) {
            g.highscore = g.skor;
            simpanHighScore(g.highscore);
        }
        if (hasil == HASIL_KELUAR) break;
        if (hasil == HASIL_MENANG) {
            if (!layarMenang(g)) break;
        } else {
            if (!layarGameOver(g)) break;
        }
    }

    endwin();
    return 0;
}