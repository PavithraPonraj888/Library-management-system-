/*
 * Library Management System (C)
 *
 * Departments : Maths, Physics, Chemistry
 * Admin       : add / update / remove books, view members, issued books, transactions
 * Member      : register, login, borrow (OWN department only), return, renew,
 *               view current books (nearest due date first, colour coded), history
 * Main search : open to everyone, view only (all departments)
 *
 * Rules : 30 days free  |  fine Rs 1 per day after due date  |  2 renewals (30 days each)
 *
 * Admin login  ->  username: admin   password: admin123
 *
 * Compile : gcc library_system.c -o library_system
 * Run     : ./library_system
 *
 * Data is saved in books.dat, members.dat, issues.dat, trans.dat (same folder).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define MAX_BOOKS     500
#define MAX_MEMBERS   200
#define MAX_ISSUES    1000
#define MAX_TRANS     5000

#define NUM_DEPTS     3
#define LOAN_DAYS     30
#define FINE_PER_DAY  1
#define MAX_RENEWALS  2
#define SOON_DAYS     3
#define DAY_SECONDS   86400L

#define ADMIN_USER    "admin"
#define ADMIN_PASS    "admin123"

/* terminal colours */
#define RED     "\033[1;31m"
#define YELLOW  "\033[1;33m"
#define GREEN   "\033[1;32m"
#define RESET   "\033[0m"

static const char *DEPT_NAMES[NUM_DEPTS] = {"Maths", "Physics", "Chemistry"};

typedef struct {
    int  id;
    char title[100];
    char author[60];
    int  dept;        /* 0 Maths, 1 Physics, 2 Chemistry */
    int  total;       /* total copies                    */
    int  available;   /* copies on the shelf             */
} Book;

typedef struct {
    char username[30];
    char password[30];
    int  dept;
} Member;

typedef struct {
    char   username[30];
    int    bookId;
    time_t dueDate;
    int    renewals;
} Issue;

typedef struct {
    time_t date;
    char   username[30];
    int    bookId;
    char   action[10];   /* BORROW / RETURN / RENEW */
    int    fine;
} Trans;

static Book   books[MAX_BOOKS];
static Member members[MAX_MEMBERS];
static Issue  issues[MAX_ISSUES];
static Trans  trans[MAX_TRANS];
static int nBooks = 0, nMembers = 0, nIssues = 0, nTrans = 0;

/* =========================== input helpers =========================== */

static void readLine(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    if (!fgets(buf, size, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\n")] = '\0';
}

static int readInt(const char *prompt) {
    char buf[32];
    readLine(prompt, buf, sizeof buf);
    return atoi(buf);
}

static int readDept(void) {
    printf("Select department:\n");
    for (int i = 0; i < NUM_DEPTS; i++) printf("  %d. %s\n", i + 1, DEPT_NAMES[i]);
    int d = readInt("Choice: ");
    return (d >= 1 && d <= NUM_DEPTS) ? d - 1 : -1;
}

static int equalsIgnoreCase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int containsIgnoreCase(const char *text, const char *key) {
    size_t n = strlen(key);
    if (n == 0) return 1;
    for (size_t i = 0; text[i]; i++) {
        size_t j = 0;
        while (j < n && text[i + j] &&
               tolower((unsigned char)text[i + j]) == tolower((unsigned char)key[j])) j++;
        if (j == n) return 1;
    }
    return 0;
}

/* ============================ date helpers =========================== */

static void formatDate(time_t t, char *out, size_t size) {
    struct tm *tmv = localtime(&t);
    strftime(out, size, "%d-%m-%Y", tmv);
}

/* days past the due date (0 if not overdue) - a part of a day counts as a day */
static int overdueDays(time_t due) {
    time_t now = time(NULL);
    if (now <= due) return 0;
    return (int)((now - due + DAY_SECONDS - 1) / DAY_SECONDS);
}

/* days remaining until due date (0 if overdue) */
static int daysLeft(time_t due) {
    time_t now = time(NULL);
    if (now >= due) return 0;
    return (int)((due - now + DAY_SECONDS - 1) / DAY_SECONDS);
}

/* ============================ file storage =========================== */

static void loadFile(const char *name, void *data, size_t size, int max, int *count) {
    FILE *f = fopen(name, "rb");
    if (!f) { *count = 0; return; }
    *count = (int)fread(data, size, max, f);
    fclose(f);
}

static void saveFile(const char *name, const void *data, size_t size, int count) {
    FILE *f = fopen(name, "wb");
    if (!f) { printf("Error: cannot save %s\n", name); return; }
    fwrite(data, size, count, f);
    fclose(f);
}

static void loadAll(void) {
    loadFile("books.dat",   books,   sizeof(Book),   MAX_BOOKS,   &nBooks);
    loadFile("members.dat", members, sizeof(Member), MAX_MEMBERS, &nMembers);
    loadFile("issues.dat",  issues,  sizeof(Issue),  MAX_ISSUES,  &nIssues);
    loadFile("trans.dat",   trans,   sizeof(Trans),   MAX_TRANS,  &nTrans);
}

static void saveBooks(void)   { saveFile("books.dat",   books,   sizeof(Book),   nBooks);   }
static void saveMembers(void) { saveFile("members.dat", members, sizeof(Member), nMembers); }
static void saveIssues(void)  { saveFile("issues.dat",  issues,  sizeof(Issue),  nIssues);  }
static void saveTrans(void)   { saveFile("trans.dat",   trans,   sizeof(Trans),  nTrans);   }

/* ============================== lookups ============================== */

static int findBook(int id) {
    for (int i = 0; i < nBooks; i++) if (books[i].id == id) return i;
    return -1;
}

static int findMember(const char *username) {
    for (int i = 0; i < nMembers; i++)
        if (equalsIgnoreCase(members[i].username, username)) return i;
    return -1;
}

static int findIssue(const char *username, int bookId) {
    for (int i = 0; i < nIssues; i++)
        if (equalsIgnoreCase(issues[i].username, username) && issues[i].bookId == bookId) return i;
    return -1;
}

static int nextBookId(void) {
    int max = 0;
    for (int i = 0; i < nBooks; i++) if (books[i].id > max) max = books[i].id;
    return max + 1;
}

static void logTrans(const char *username, int bookId, const char *action, int fine) {
    if (nTrans >= MAX_TRANS) return;
    Trans *t = &trans[nTrans++];
    t->date = time(NULL);
    strncpy(t->username, username, sizeof t->username - 1);
    t->username[sizeof t->username - 1] = '\0';
    t->bookId = bookId;
    strncpy(t->action, action, sizeof t->action - 1);
    t->action[sizeof t->action - 1] = '\0';
    t->fine = fine;
    saveTrans();
}

/* ============================== printing ============================= */

static void printBookHeader(void) {
    printf("\n%-5s %-30s %-20s %-10s %-6s %-9s\n",
           "ID", "Title", "Author", "Dept", "Total", "Available");
    printf("------------------------------------------------------------------------------------\n");
}

static void printBook(const Book *b) {
    printf("%-5d %-30.30s %-20.20s %-10s %-6d %-9d\n",
           b->id, b->title, b->author, DEPT_NAMES[b->dept], b->total, b->available);
}

/* search by ID / title / author. dept < 0 means all departments */
static void searchBooks(int dept) {
    int found = 0;
    printf("\nSearch by:\n  1. Book ID\n  2. Title\n  3. Author\n");
    int opt = readInt("Choice: ");
    if (opt < 1 || opt > 3) { printf("Invalid choice.\n"); return; }

    int id = 0;
    char key[100] = "";
    if (opt == 1) id = readInt("Enter book ID: ");
    else readLine("Enter keyword: ", key, sizeof key);

    printBookHeader();
    for (int i = 0; i < nBooks; i++) {
        if (dept >= 0 && books[i].dept != dept) continue;
        int match = 0;
        if (opt == 1) match = (books[i].id == id);
        else if (opt == 2) match = containsIgnoreCase(books[i].title, key);
        else match = containsIgnoreCase(books[i].author, key);
        if (match) { printBook(&books[i]); found++; }
    }
    if (!found) printf("(no books found)\n");
}

/* ================================ ADMIN ============================== */

static void addBook(void) {
    Book b;
    if (nBooks >= MAX_BOOKS) { printf("Library is full!\n"); return; }

    readLine("Title  : ", b.title, sizeof b.title);
    readLine("Author : ", b.author, sizeof b.author);
    b.dept = readDept();
    b.total = readInt("Copies : ");
    if (b.title[0] == '\0' || b.author[0] == '\0' || b.dept < 0 || b.total <= 0) {
        printf("Invalid input. Book not added.\n");
        return;
    }
    b.id = nextBookId();
    b.available = b.total;
    books[nBooks++] = b;
    saveBooks();
    printf("Book added successfully. Book ID = %d\n", b.id);
}

static void updateBook(void) {
    int ch;
    do {
        printf("\n--- UPDATE BOOK ---\n");
        printf(" 1. Add copies\n");
        printf(" 2. Update title\n");
        printf(" 3. Update author\n");
        printf(" 4. Update department\n");
        printf(" 0. Back\n");
        ch = readInt("Choice: ");
        if (ch < 1 || ch > 4) { if (ch != 0) printf("Invalid choice.\n"); continue; }

        int id = readInt("Book ID: ");
        int idx = findBook(id);
        if (idx < 0) { printf("Book not found.\n"); continue; }

        if (ch == 1) {
            int n = readInt("Copies to add: ");
            if (n <= 0) { printf("Invalid number.\n"); continue; }
            books[idx].total += n;
            books[idx].available += n;
            saveBooks();
            printf("%d copies added successfully.\n", n);
        } else if (ch == 2) {
            char t[100];
            readLine("New title: ", t, sizeof t);
            if (t[0] == '\0') { printf("Title cannot be empty.\n"); continue; }
            strcpy(books[idx].title, t);
            saveBooks();
            printf("Title updated successfully.\n");
        } else if (ch == 3) {
            char a[60];
            readLine("New author: ", a, sizeof a);
            if (a[0] == '\0') { printf("Author cannot be empty.\n"); continue; }
            strcpy(books[idx].author, a);
            saveBooks();
            printf("Author updated successfully.\n");
        } else {
            int d = readDept();
            if (d < 0) { printf("Invalid department.\n"); continue; }
            books[idx].dept = d;
            saveBooks();
            printf("Department updated successfully.\n");
        }
    } while (ch != 0);
}

static void removeBook(void) {
    int id = readInt("Book ID to remove: ");
    int idx = findBook(id);
    if (idx < 0) { printf("Book not found.\n"); return; }
    if (books[idx].available != books[idx].total) {
        printf("Cannot remove: some copies are currently issued.\n");
        return;
    }
    for (int i = idx; i < nBooks - 1; i++) books[i] = books[i + 1];
    nBooks--;
    saveBooks();
    printf("Book removed successfully.\n");
}

static void viewMembers(void) {
    printf("\n%-5s %-30s %-10s\n", "No.", "Username", "Dept");
    printf("-----------------------------------------\n");
    if (nMembers == 0) printf("(no members)\n");
    for (int i = 0; i < nMembers; i++)
        printf("%-5d %-30.30s %-10s\n", i + 1, members[i].username, DEPT_NAMES[members[i].dept]);
}

static void viewIssuedBooks(void) {
    char d[16];
    printf("\n%-15s %-6s %-28s %-12s %-6s\n", "Member", "BookID", "Title", "Due Date", "Fine");
    printf("--------------------------------------------------------------------------\n");
    if (nIssues == 0) printf("(no books issued)\n");
    for (int i = 0; i < nIssues; i++) {
        int b = findBook(issues[i].bookId);
        int od = overdueDays(issues[i].dueDate);
        formatDate(issues[i].dueDate, d, sizeof d);
        printf("%s%-15.15s %-6d %-28.28s %-12s Rs %-3d%s\n",
               od > 0 ? RED : "", issues[i].username, issues[i].bookId,
               b >= 0 ? books[b].title : "?", d, od * FINE_PER_DAY,
               od > 0 ? RESET : "");
    }
}

static void printTransHeader(void) {
    printf("\n%-12s %-15s %-6s %-8s %-6s\n", "Date", "Member", "BookID", "Action", "Fine");
    printf("-----------------------------------------------------\n");
}

static void printTrans(const Trans *t) {
    char d[16];
    formatDate(t->date, d, sizeof d);
    printf("%-12s %-15.15s %-6d %-8s Rs %-3d\n", d, t->username, t->bookId, t->action, t->fine);
}

static void viewTransactions(void) {
    printf("\n--- TRANSACTION RECORDS ---\n");
    printf(" 1. All records\n 2. By member username\n 3. By book ID\n");
    int ch = readInt("Choice: ");
    char user[30] = "";
    int bookId = 0, found = 0;

    if (ch == 2) readLine("Username: ", user, sizeof user);
    else if (ch == 3) bookId = readInt("Book ID: ");
    else if (ch != 1) { printf("Invalid choice.\n"); return; }

    printTransHeader();
    for (int i = 0; i < nTrans; i++) {
        if (ch == 2 && !equalsIgnoreCase(trans[i].username, user)) continue;
        if (ch == 3 && trans[i].bookId != bookId) continue;
        printTrans(&trans[i]);
        found++;
    }
    if (!found) printf("(no records)\n");
}

static void adminMenu(void) {
    int ch;
    do {
        printf("\n========== ADMIN MENU ==========\n");
        printf(" 1. Add new book\n");
        printf(" 2. Update book\n");
        printf(" 3. Remove book\n");
        printf(" 4. Search books\n");
        printf(" 5. View members\n");
        printf(" 6. View issued books\n");
        printf(" 7. Transaction records\n");
        printf(" 0. Logout\n");
        ch = readInt("Choice: ");
        switch (ch) {
            case 1: addBook(); break;
            case 2: updateBook(); break;
            case 3: removeBook(); break;
            case 4: searchBooks(-1); break;
            case 5: viewMembers(); break;
            case 6: viewIssuedBooks(); break;
            case 7: viewTransactions(); break;
            case 0: printf("Logged out.\n"); break;
            default: printf("Invalid choice.\n");
        }
    } while (ch != 0);
}

static void adminLogin(void) {
    char user[30], pass[30];
    readLine("Admin username: ", user, sizeof user);
    readLine("Password: ", pass, sizeof pass);
    if (equalsIgnoreCase(user, ADMIN_USER) && strcmp(pass, ADMIN_PASS) == 0)
        adminMenu();
    else
        printf("Invalid admin username or password.\n");
}

/* ================================ MEMBER ============================= */

static void borrowBook(const Member *m) {
    int id = readInt("Book ID to borrow: ");
    int idx = findBook(id);
    if (idx < 0) { printf("Book not found.\n"); return; }

    /* students can borrow only from their own department */
    if (books[idx].dept != m->dept) {
        printf("Access denied: this book belongs to another department.\n");
        return;
    }
    if (books[idx].available <= 0) { printf("Sorry, no copies available.\n"); return; }
    if (findIssue(m->username, id) >= 0) { printf("You have already borrowed this book.\n"); return; }
    if (nIssues >= MAX_ISSUES) { printf("Issue limit reached.\n"); return; }

    Issue *is = &issues[nIssues++];
    memset(is, 0, sizeof *is);
    strcpy(is->username, m->username);
    is->bookId = id;
    is->dueDate = time(NULL) + LOAN_DAYS * DAY_SECONDS;
    is->renewals = 0;
    books[idx].available--;

    saveIssues();
    saveBooks();
    logTrans(m->username, id, "BORROW", 0);

    char d[16];
    formatDate(is->dueDate, d, sizeof d);
    printf("Book borrowed successfully. Due date: %s\n", d);
}

static void returnBook(const Member *m) {
    int id = readInt("Book ID to return: ");
    int iIdx = findIssue(m->username, id);
    if (iIdx < 0) { printf("You have not borrowed this book.\n"); return; }

    int fine = overdueDays(issues[iIdx].dueDate) * FINE_PER_DAY;

    for (int i = iIdx; i < nIssues - 1; i++) issues[i] = issues[i + 1];
    nIssues--;

    int bIdx = findBook(id);
    if (bIdx >= 0) books[bIdx].available++;

    saveIssues();
    saveBooks();
    logTrans(m->username, id, "RETURN", fine);

    printf("Book returned successfully.\n");
    if (fine > 0) printf("Book was overdue. Fine to pay: Rs %d\n", fine);
    else          printf("No fine.\n");
}

static void renewBook(const Member *m) {
    int id = readInt("Book ID to renew: ");
    int iIdx = findIssue(m->username, id);
    if (iIdx < 0) { printf("You have not borrowed this book.\n"); return; }

    if (overdueDays(issues[iIdx].dueDate) > 0) {
        printf("Cannot renew: book is overdue. Please return it (fine applies).\n");
        return;
    }
    if (issues[iIdx].renewals >= MAX_RENEWALS) {
        printf("Cannot renew: maximum %d renewals reached.\n", MAX_RENEWALS);
        return;
    }
    issues[iIdx].dueDate += LOAN_DAYS * DAY_SECONDS;
    issues[iIdx].renewals++;
    saveIssues();
    logTrans(m->username, id, "RENEW", 0);

    char d[16];
    formatDate(issues[iIdx].dueDate, d, sizeof d);
    printf("Book renewed successfully. New due date: %s (renewals used: %d/%d)\n",
           d, issues[iIdx].renewals, MAX_RENEWALS);
}

static int compareDue(const void *a, const void *b) {
    const Issue *x = a, *y = b;
    if (x->dueDate < y->dueDate) return -1;
    if (x->dueDate > y->dueDate) return 1;
    return 0;
}

/* current books, nearest due date first, colour coded */
static void myCurrentBooks(const Member *m) {
    Issue mine[MAX_ISSUES];
    int n = 0;
    for (int i = 0; i < nIssues; i++)
        if (equalsIgnoreCase(issues[i].username, m->username)) mine[n++] = issues[i];

    if (n == 0) { printf("You have no books currently borrowed.\n"); return; }
    qsort(mine, n, sizeof(Issue), compareDue);

    printf("\n%-6s %-28s %-12s %-22s\n", "BookID", "Title", "Due Date", "Status");
    printf("---------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        int b = findBook(mine[i].bookId);
        int od = overdueDays(mine[i].dueDate);
        int left = daysLeft(mine[i].dueDate);
        char d[16], status[48];
        const char *color;

        formatDate(mine[i].dueDate, d, sizeof d);
        if (od > 0) {
            color = RED;
            snprintf(status, sizeof status, "OVERDUE %d days, fine Rs %d", od, od * FINE_PER_DAY);
        } else if (left <= SOON_DAYS) {
            color = YELLOW;
            snprintf(status, sizeof status, "Due soon (%d days left)", left);
        } else {
            color = GREEN;
            snprintf(status, sizeof status, "%d days left", left);
        }
        printf("%s%-6d %-28.28s %-12s %s%s\n", color, mine[i].bookId,
               b >= 0 ? books[b].title : "?", d, status, RESET);
    }
    printf("\n" RED "Red" RESET " = overdue   " YELLOW "Yellow" RESET
           " = due within %d days   " GREEN "Green" RESET " = safe\n", SOON_DAYS);
}

static void myHistory(const Member *m) {
    int found = 0;
    printTransHeader();
    for (int i = 0; i < nTrans; i++) {
        if (equalsIgnoreCase(trans[i].username, m->username)) { printTrans(&trans[i]); found++; }
    }
    if (!found) printf("(no history)\n");
}

static void memberMenu(const Member *m) {
    int ch;
    do {
        printf("\n===== MEMBER MENU (%s - %s Dept) =====\n", m->username, DEPT_NAMES[m->dept]);
        printf(" 1. Search books (my department)\n");
        printf(" 2. Borrow a book\n");
        printf(" 3. Return a book\n");
        printf(" 4. Renew a book\n");
        printf(" 5. My current books\n");
        printf(" 6. My borrowing history\n");
        printf(" 0. Logout\n");
        ch = readInt("Choice: ");
        switch (ch) {
            case 1: searchBooks(m->dept); break;
            case 2: borrowBook(m); break;
            case 3: returnBook(m); break;
            case 4: renewBook(m); break;
            case 5: myCurrentBooks(m); break;
            case 6: myHistory(m); break;
            case 0: printf("Logged out.\n"); break;
            default: printf("Invalid choice.\n");
        }
    } while (ch != 0);
}

static void registerMember(void) {
    Member m;
    if (nMembers >= MAX_MEMBERS) { printf("Member list is full!\n"); return; }

    readLine("Choose username: ", m.username, sizeof m.username);
    if (m.username[0] == '\0' || strchr(m.username, ' ')) {
        printf("Invalid username (no spaces allowed).\n");
        return;
    }
    if (equalsIgnoreCase(m.username, ADMIN_USER) || findMember(m.username) >= 0) {
        printf("Username already taken.\n");
        return;
    }
    readLine("Choose password: ", m.password, sizeof m.password);
    if (m.password[0] == '\0') { printf("Password cannot be empty.\n"); return; }
    m.dept = readDept();
    if (m.dept < 0) { printf("Invalid department.\n"); return; }

    members[nMembers++] = m;
    saveMembers();
    printf("Registration successful! You can now login.\n");
}

static void memberLogin(void) {
    char user[30], pass[30];
    readLine("Username: ", user, sizeof user);
    readLine("Password: ", pass, sizeof pass);
    int idx = findMember(user);
    if (idx >= 0 && strcmp(members[idx].password, pass) == 0)
        memberMenu(&members[idx]);
    else
        printf("Invalid username or password.\n");
}

static void userMenu(void) {
    int ch;
    do {
        printf("\n--- USER ---\n");
        printf(" 1. Register (new user)\n");
        printf(" 2. Login\n");
        printf(" 0. Back\n");
        ch = readInt("Choice: ");
        switch (ch) {
            case 1: registerMember(); break;
            case 2: memberLogin(); break;
            case 0: break;
            default: printf("Invalid choice.\n");
        }
    } while (ch != 0);
}

/* ================================= MAIN ============================== */

int main(void) {
    int ch;
    loadAll();
    do {
        printf("\n=====================================\n");
        printf("     LIBRARY MANAGEMENT SYSTEM\n");
        printf("  Depts: Maths | Physics | Chemistry\n");
        printf("=====================================\n");
        printf(" 1. Admin login\n");
        printf(" 2. User (Register / Login)\n");
        printf(" 3. Search books (view only)\n");
        printf(" 0. Exit\n");
        ch = readInt("Choice: ");
        switch (ch) {
            case 1: adminLogin(); break;
            case 2: userMenu(); break;
            case 3: searchBooks(-1); break;   /* all departments, view only */
            case 0: printf("Goodbye!\n"); break;
            default: printf("Invalid choice.\n");
        }
    } while (ch != 0);
    return 0;
}
