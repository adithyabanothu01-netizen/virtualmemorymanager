/**
 * ============================================================================
 * VIRTUAL MEMORY MANAGER - C++ WEB SERVER & OS SIMULATION ENGINE
 * ============================================================================
 * Features:
 *  - Zero-dependency cross-platform C++ HTTP server (Linux, macOS, Windows)
 *  - Doubly Linked List for address-ordered contiguous memory blocks
 *  - Hash Table (std::unordered_map) for O(1) Process ID to Block lookup
 *  - Queue (std::queue) for blocked process allocation requests
 *  - First Fit & Best Fit contiguous allocation strategies
 *  - Automatic adjacent free-block coalescing & External Fragmentation alerts
 *  - Memory Compaction engine & embedded interactive Web Dashboard
 *
 * Compilation:
 *  - Linux / macOS:  g++ -std=c++11 -O2 vmm_server.cpp -o vmm_server
 *  - Windows (MinGW): g++ -std=c++11 -O2 vmm_server.cpp -o vmm_server.exe -lws2_32
 *
 * Run:
 *  ./vmm_server
 *  Then open http://localhost:8080 in your browser.
 * ============================================================================
 */

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>

// Cross-platform socket includes
#ifdef _WIN32
  #define _WIN32_WINNT 0x0601
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET SocketType;
  #define CLOSE_SOCKET closesocket
  #define INVALID_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int SocketType;
  #define CLOSE_SOCKET close
  #define INVALID_SOCK -1
#endif

// ============================================================================
// 1. OPERATING SYSTEM DATA STRUCTURES & VIRTUAL MEMORY MANAGER CLASS
// ============================================================================

enum Strategy {
    FIRST_FIT,
    BEST_FIT
};

// Doubly Linked List Node representing a contiguous memory segment
struct Block {
    int id;
    int start_address;
    int size;
    std::string pid;
    bool is_free;
    Block* prev;
    Block* next;

    Block(int node_id, int addr, int sz, const std::string& process_id = "", bool free_status = true)
        : id(node_id), start_address(addr), size(sz), pid(process_id),
          is_free(free_status), prev(nullptr), next(nullptr) {}
};

// Queue item for processes waiting for contiguous memory
struct ProcessRequest {
    std::string pid;
    int size;
};

// Console log item streamed to the web UI terminal
struct LogEntry {
    std::string timestamp;
    std::string type;
    std::string message;
};

class VirtualMemoryManager {
private:
    Block* head;
    Block* tail;
    int total_memory_size;
    int next_node_id;
    Strategy current_strategy;

    // Hash table mapping PID -> DLL Block* for direct O(1) lookup
    std::unordered_map<std::string, Block*> allocated_map;

    // Queue holding waiting process allocation requests
    std::queue<ProcessRequest> waiting_queue;

    // Activity logs for the UI shell
    std::vector<LogEntry> logs;

    // Helper: Get current HH:MM:SS timestamp
    std::string getCurrentTime() const {
        std::time_t now = std::time(nullptr);
        std::tm* local_tm = std::localtime(&now);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", local_tm);
        return std::string(buf);
    }

    // Helper: Escape special characters for valid JSON output
    static std::string escapeJson(const std::string& s) {
        std::ostringstream o;
        for (size_t i = 0; i < s.length(); ++i) {
            switch (s[i]) {
                case '"':  o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b";  break;
                case '\f': o << "\\f";  break;
                case '\n': o << "\\n";  break;
                case '\r': o << "\\r";  break;
                case '\t': o << "\\t";  break;
                default:
                    if ('\x00' <= s[i] && s[i] <= '\x1f') {
                        o << "\\u" << std::hex << (int)s[i];
                    } else {
                        o << s[i];
                    }
            }
        }
        return o.str();
    }

    // Helper: Uppercase and trim whitespace from PID strings
    static std::string normalizePid(const std::string& raw) {
        std::string res;
        for (char c : raw) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                res += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return res;
    }

    // Helper: Clear all Doubly Linked List nodes safely
    void clearList() {
        Block* curr = head;
        while (curr != nullptr) {
            Block* nxt = curr->next;
            delete curr;
            curr = nxt;
        }
        head = nullptr;
        tail = nullptr;
    }

    // Strategy search: First Fit vs Best Fit
    Block* findBlock(int size) {
        Block* curr = head;
        Block* chosen = nullptr;

        if (current_strategy == FIRST_FIT) {
            while (curr != nullptr) {
                if (curr->is_free && curr->size >= size) {
                    return curr;
                }
                curr = curr->next;
            }
        } else if (current_strategy == BEST_FIT) {
            int min_remainder = 2147483647;
            while (curr != nullptr) {
                if (curr->is_free && curr->size >= size) {
                    int remainder = curr->size - size;
                    if (remainder < min_remainder) {
                        min_remainder = remainder;
                        chosen = curr;
                    }
                }
                curr = curr->next;
            }
        }
        return chosen;
    }

public:
    VirtualMemoryManager(int size = 1000)
        : head(nullptr), tail(nullptr), total_memory_size(size),
          next_node_id(1), current_strategy(FIRST_FIT) {
        reset(size);
    }

    ~VirtualMemoryManager() {
        clearList();
    }

    void addLog(const std::string& type, const std::string& msg) {
        logs.push_back({getCurrentTime(), type, msg});
        if (logs.size() > 120) {
            logs.erase(logs.begin());
        }
        std::cout << "[" << type << "] " << msg << std::endl;
    }

    void clearLogs() {
        logs.clear();
        addLog("SYSTEM", "C++ terminal log cleared.");
    }

    // Re-initialize memory space
    void reset(int size) {
        if (size < 100) size = 100;
        if (size > 100000) size = 100000;

        clearList();
        allocated_map.clear();
        while (!waiting_queue.empty()) waiting_queue.pop();

        total_memory_size = size;
        next_node_id = 1;

        head = new Block(next_node_id++, 0, total_memory_size, "", true);
        tail = head;

        addLog("SYSTEM", "Initialized C++ Virtual Memory Manager with " + std::to_string(total_memory_size) + " KB RAM.");
    }

    void setStrategy(Strategy s) {
        current_strategy = s;
        addLog("SYSTEM", std::string("Allocation strategy set to ") +
               (s == FIRST_FIT ? "FIRST FIT" : "BEST FIT") + ".");
    }

    // Allocate contiguous memory for a process
    bool allocate(const std::string& raw_pid, int size, bool from_queue = false) {
        std::string pid = normalizePid(raw_pid);
        if (pid.empty()) {
            addLog("ERROR", "Process ID cannot be empty.");
            return false;
        }
        if (size <= 0) {
            addLog("ERROR", "Allocation size must be greater than 0 KB.");
            return false;
        }
        if (allocated_map.find(pid) != allocated_map.end()) {
            addLog("ERROR", "Process " + pid + " already exists in RAM!");
            return false;
        }

        Block* target = findBlock(size);

        if (target != nullptr) {
            if (target->size == size) {
                // Exact fit
                target->is_free = false;
                target->pid = pid;
            } else {
                // Split block into allocated block + remaining free node
                Block* new_free = new Block(
                    next_node_id++,
                    target->start_address + size,
                    target->size - size,
                    "",
                    true
                );
                new_free->prev = target;
                new_free->next = target->next;

                if (target->next != nullptr) {
                    target->next->prev = new_free;
                } else {
                    tail = new_free;
                }

                target->next = new_free;
                target->size = size;
                target->is_free = false;
                target->pid = pid;
            }

            // Record in C++ Hash Table
            allocated_map[pid] = target;

            std::ostringstream oss;
            oss << "Allocated " << size << " KB to Process " << pid
                << " at address [" << target->start_address << " - "
                << (target->start_address + size - 1) << "].";
            addLog("SUCCESS", oss.str());
            return true;
        } else {
            int total_free = getTotalFreeMemory();
            int max_contig = getMaxContiguousFreeMemory();

            addLog("WARNING", "Could not allocate " + std::to_string(size) + " KB for Process " + pid + ".");

            if (total_free >= size && max_contig < size) {
                std::ostringstream oss;
                oss << "EXTERNAL FRAGMENTATION DETECTED! Total free RAM is " << total_free
                    << " KB, but largest contiguous block is " << max_contig << " KB.";
                addLog("FRAG", oss.str());
            } else {
                std::ostringstream oss;
                oss << "Insufficient total free RAM (" << total_free << " KB available vs "
                    << size << " KB requested).";
                addLog("INFO", oss.str());
            }

            if (!from_queue) {
                waiting_queue.push({pid, size});
                addLog("QUEUE", "Process " + pid + " (" + std::to_string(size) + " KB) enqueued in std::queue.");
            }
            return false;
        }
    }

    // Release process memory via O(1) Hash Table lookup & coalesce free neighbors
    bool freeMemory(const std::string& raw_pid) {
        std::string pid = normalizePid(raw_pid);
        auto it = allocated_map.find(pid);
        if (it == allocated_map.end()) {
            addLog("ERROR", "Process " + pid + " not found in allocated hash table.");
            return false;
        }

        Block* curr = it->second;
        int released_size = curr->size;

        curr->is_free = true;
        curr->pid = "";
        allocated_map.erase(it);

        addLog("SUCCESS", "Process " + pid + " released " + std::to_string(released_size) + " KB of RAM.");

        // Coalesce with next block if free
        if (curr->next != nullptr && curr->next->is_free) {
            Block* nxt = curr->next;
            curr->size += nxt->size;
            curr->next = nxt->next;
            if (nxt->next != nullptr) {
                nxt->next->prev = curr;
            } else {
                tail = curr;
            }
            delete nxt;
        }

        // Coalesce with previous block if free
        if (curr->prev != nullptr && curr->prev->is_free) {
            Block* prv = curr->prev;
            prv->size += curr->size;
            prv->next = curr->next;
            if (curr->next != nullptr) {
                curr->next->prev = prv;
            } else {
                tail = prv;
            }
            delete curr;
        }

        serviceWaitingQueue();
        return true;
    }

    // Compact memory: pack allocated blocks at address 0, combine free RAM at end
    void compact() {
        addLog("COMPACT", "Executing C++ memory compaction across Doubly Linked List...");

        Block* curr = head;
        int current_addr = 0;
        int total_free = 0;

        while (curr != nullptr) {
            Block* nxt = curr->next;
            if (curr->is_free) {
                total_free += curr->size;
                if (curr->prev != nullptr) curr->prev->next = curr->next;
                else head = curr->next;

                if (curr->next != nullptr) curr->next->prev = curr->prev;
                else tail = curr->prev;

                delete curr;
            } else {
                curr->start_address = current_addr;
                current_addr += curr->size;
            }
            curr = nxt;
        }

        if (head == nullptr) {
            head = new Block(next_node_id++, 0, total_free, "", true);
            tail = head;
        } else if (total_free > 0) {
            Block* merged_free = new Block(next_node_id++, current_addr, total_free, "", true);
            tail->next = merged_free;
            merged_free->prev = tail;
            tail = merged_free;
        }

        addLog("COMPACT", "Compaction complete! Consolidated free space into one " + std::to_string(total_free) + " KB block.");
        serviceWaitingQueue();
    }

    // Retry blocked requests in std::queue
    void serviceWaitingQueue() {
        if (waiting_queue.empty()) return;

        size_t count = waiting_queue.size();
        addLog("QUEUE", "Checking waiting queue (" + std::to_string(count) + " blocked processes)...");

        for (size_t i = 0; i < count; ++i) {
            ProcessRequest req = waiting_queue.front();
            waiting_queue.pop();

            if (findBlock(req.size) != nullptr) {
                addLog("QUEUE", "Dequeued and servicing Process " + req.pid + " (" + std::to_string(req.size) + " KB)...");
                allocate(req.pid, req.size, true);
            } else {
                waiting_queue.push(req);
            }
        }
    }

    // Remove a specific index from waiting queue
    void cancelQueueItem(int index) {
        if (index < 0 || index >= static_cast<int>(waiting_queue.size())) return;
        std::queue<ProcessRequest> updated;
        int idx = 0;
        while (!waiting_queue.empty()) {
            ProcessRequest req = waiting_queue.front();
            waiting_queue.pop();
            if (idx == index) {
                addLog("QUEUE", "Cancelled queued request for Process " + req.pid + ".");
            } else {
                updated.push(req);
            }
            idx++;
        }
        waiting_queue = updated;
    }

    // Run built-in test scenarios
    void runPreset(const std::string& preset) {
        reset(1000);
        if (preset == "frag") {
            addLog("SYSTEM", "--- Preset: External Fragmentation Trigger ---");
            allocate("P1", 250);
            allocate("P2", 200);
            allocate("P3", 300);
            allocate("P4", 150);
            freeMemory("P2");
            freeMemory("P4");
            allocate("P5", 300); // Triggers external fragmentation!
        } else if (preset == "strategy") {
            addLog("SYSTEM", "--- Preset: First Fit vs Best Fit Comparison ---");
            allocate("P1", 300);
            allocate("P2", 150);
            allocate("P3", 200);
            allocate("P4", 200);
            freeMemory("P1"); // 300 KB hole at [0..299]
            freeMemory("P3"); // 200 KB hole at [450..649]
            allocate("P5", 150); // First Fit picks [0..299]; Best Fit picks [450..649]!
        } else if (preset == "heavy") {
            addLog("SYSTEM", "--- Preset: Heavy Multi-Process Workload ---");
            allocate("P1", 300);
            allocate("P2", 250);
            allocate("P3", 200);
            allocate("P4", 150);
            allocate("P5", 200); // Enters waiting queue
        }
    }

    int getTotalFreeMemory() const {
        int total = 0;
        Block* curr = head;
        while (curr != nullptr) {
            if (curr->is_free) total += curr->size;
            curr = curr->next;
        }
        return total;
    }

    int getMaxContiguousFreeMemory() const {
        int max_c = 0;
        Block* curr = head;
        while (curr != nullptr) {
            if (curr->is_free && curr->size > max_c) {
                max_c = curr->size;
            }
            curr = curr->next;
        }
        return max_c;
    }

    // Serialize entire C++ simulator state to JSON for the web UI
    std::string toJson() const {
        int total_free = getTotalFreeMemory();
        int used_mem = total_memory_size - total_free;
        int max_contig = getMaxContiguousFreeMemory();

        int node_count = 0;
        int free_node_count = 0;
        for (Block* c = head; c != nullptr; c = c->next) {
            node_count++;
            if (c->is_free) free_node_count++;
        }

        // Check if any queued process is stalled due to external fragmentation
        bool has_frag = false;
        std::string frag_pid = "";
        int frag_req_size = 0;
        std::queue<ProcessRequest> q_copy = waiting_queue;
        while (!q_copy.empty()) {
            ProcessRequest r = q_copy.front();
            q_copy.pop();
            if (total_free >= r.size && max_contig < r.size) {
                has_frag = true;
                frag_pid = r.pid;
                frag_req_size = r.size;
                break;
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"totalMemory\":" << total_memory_size << ",";
        json << "\"usedMemory\":" << used_mem << ",";
        json << "\"freeMemory\":" << total_free << ",";
        json << "\"maxContiguous\":" << max_contig << ",";
        json << "\"nodeCount\":" << node_count << ",";
        json << "\"freeNodeCount\":" << free_node_count << ",";
        json << "\"strategy\":\"" << (current_strategy == FIRST_FIT ? "FIRST_FIT" : "BEST_FIT") << "\",";

        // Fragmentation status
        json << "\"fragmentation\":{";
        json << "\"detected\":" << (has_frag ? "true" : "false") << ",";
        json << "\"pid\":\"" << escapeJson(frag_pid) << "\",";
        json << "\"requestedSize\":" << frag_req_size;
        json << "},";

        // Doubly Linked List nodes
        json << "\"blocks\":[";
        Block* curr = head;
        bool first = true;
        while (curr != nullptr) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"id\":" << curr->id << ",";
            json << "\"start\":" << curr->start_address << ",";
            json << "\"end\":" << (curr->start_address + curr->size - 1) << ",";
            json << "\"size\":" << curr->size << ",";
            json << "\"isFree\":" << (curr->is_free ? "true" : "false") << ",";
            json << "\"pid\":\"" << escapeJson(curr->pid) << "\",";
            json << "\"prevId\":" << (curr->prev ? curr->prev->id : -1) << ",";
            json << "\"nextId\":" << (curr->next ? curr->next->id : -1);
            json << "}";
            curr = curr->next;
        }
        json << "],";

        // Hash Table entries (sorted by PID for neat display)
        std::vector<std::string> pids;
        for (const auto& kv : allocated_map) {
            pids.push_back(kv.first);
        }
        std::sort(pids.begin(), pids.end());

        json << "\"hashTable\":[";
        for (size_t i = 0; i < pids.size(); ++i) {
            if (i > 0) json << ",";
            Block* b = allocated_map.at(pids[i]);
            json << "{";
            json << "\"pid\":\"" << escapeJson(pids[i]) << "\",";
            json << "\"nodeId\":" << b->id << ",";
            json << "\"start\":" << b->start_address << ",";
            json << "\"end\":" << (b->start_address + b->size - 1) << ",";
            json << "\"size\":" << b->size;
            json << "}";
        }
        json << "],";

        // Waiting Queue entries
        json << "\"queue\":[";
        std::queue<ProcessRequest> q_iter = waiting_queue;
        bool first_q = true;
        while (!q_iter.empty()) {
            if (!first_q) json << ",";
            first_q = false;
            ProcessRequest req = q_iter.front();
            q_iter.pop();
            json << "{\"pid\":\"" << escapeJson(req.pid) << "\",\"size\":" << req.size << "}";
        }
        json << "],";

        // Logs
        json << "\"logs\":[";
        for (size_t i = 0; i < logs.size(); ++i) {
            if (i > 0) json << ",";
            json << "{";
            json << "\"time\":\"" << escapeJson(logs[i].timestamp) << "\",";
            json << "\"type\":\"" << escapeJson(logs[i].type) << "\",";
            json << "\"message\":\"" << escapeJson(logs[i].message) << "\"";
            json << "}";
        }
        json << "]";

        json << "}";
        return json.str();
    }
};

// ============================================================================
// 2. EMBEDDED INTERACTIVE FRONTEND HTML (SERVED BY C++ HTTP SERVER)
// ============================================================================

const std::string INDEX_HTML = R"HTML(<!DOCTYPE html>
<html lang="en" class="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Virtual Memory Manager (C++ Engine)</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <script src="https://unpkg.com/lucide@latest"></script>
  <script>
    tailwind.config = {
      darkMode: 'class',
      theme: {
        extend: {
          fontFamily: {
            sans: ['Inter', 'sans-serif'],
            mono: ['JetBrains Mono', 'monospace'],
          },
          colors: {
            slate: { 850: '#131e32', 900: '#0f172a', 950: '#080d1a' }
          }
        }
      }
    }
  </script>
  <style>
    .bg-stripes-slate {
      background-image: linear-gradient(135deg, rgba(255, 255, 255, 0.04) 25%, transparent 25%, transparent 50%, rgba(255, 255, 255, 0.04) 50%, rgba(255, 255, 255, 0.04) 75%, transparent 75%, transparent);
      background-size: 16px 16px;
    }
    ::-webkit-scrollbar { width: 6px; height: 6px; }
    ::-webkit-scrollbar-track { background: #0f172a; }
    ::-webkit-scrollbar-thumb { background: #334155; border-radius: 3px; }
    ::-webkit-scrollbar-thumb:hover { background: #475569; }
    @keyframes subtle-pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.93; transform: scale(0.998); }
    }
    .animate-pulse-glow { animation: subtle-pulse 2.5s infinite ease-in-out; }
  </style>
</head>
<body class="bg-slate-950 text-slate-100 font-sans min-h-screen antialiased flex flex-col selection:bg-indigo-500 selection:text-white">

  <!-- Header -->
  <header class="border-b border-slate-800 bg-slate-900/80 backdrop-blur-md sticky top-0 z-40">
    <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 h-16 flex items-center justify-between">
      <div class="flex items-center space-x-3">
        <div class="p-2 bg-gradient-to-tr from-indigo-600 to-violet-500 rounded-lg text-white shadow-lg shadow-indigo-500/20">
          <i data-lucide="cpu" class="w-6 h-6"></i>
        </div>
        <div>
          <h1 class="text-lg font-bold tracking-tight bg-gradient-to-r from-white via-slate-200 to-slate-400 bg-clip-text text-transparent">
            Virtual Memory Manager
          </h1>
          <p class="text-xs text-slate-400 font-mono">Powered by Native C++11 OS Simulation Backend</p>
        </div>
      </div>

      <div class="flex items-center space-x-3">
        <div class="hidden md:flex items-center px-3 py-1.5 rounded-full bg-emerald-950/60 border border-emerald-700/50 text-xs font-mono text-emerald-300 space-x-2">
          <span class="w-2 h-2 rounded-full bg-emerald-400 animate-ping"></span>
          <span>C++ SERVER: <strong>CONNECTED</strong></span>
        </div>

        <div class="hidden sm:flex items-center px-3 py-1.5 rounded-full bg-slate-800/80 border border-slate-700 text-xs font-mono text-slate-300 space-x-2">
          <span>STRATEGY: <strong id="activeStrategyBadge" class="text-indigo-400">FIRST FIT</strong></span>
        </div>

        <button onclick="openResetModal()" class="flex items-center space-x-1.5 text-xs font-medium bg-slate-800 hover:bg-slate-700 text-slate-300 hover:text-white px-3 py-2 rounded-lg border border-slate-700 transition">
          <i data-lucide="rotate-ccw" class="w-3.5 h-3.5"></i>
          <span>Reset RAM</span>
        </button>
      </div>
    </div>
  </header>

  <!-- Main Content -->
  <main class="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 lg:px-8 py-6 space-y-6">

    <!-- External Fragmentation Alert Banner -->
    <div id="fragAlert" class="hidden animate-pulse-glow bg-amber-950/70 border border-amber-500/50 rounded-xl p-4 text-amber-200 shadow-2xl flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
      <div class="flex items-start space-x-3">
        <div class="p-2 bg-amber-500/20 text-amber-400 rounded-lg shrink-0 mt-0.5">
          <i data-lucide="alert-triangle" class="w-5 h-5"></i>
        </div>
        <div>
          <h4 class="font-semibold text-amber-300 text-sm flex items-center gap-2">
            EXTERNAL FRAGMENTATION DETECTED
            <span class="text-[10px] font-mono bg-amber-500/20 px-2 py-0.5 rounded border border-amber-500/30 text-amber-200">C++ Kernel Alert</span>
          </h4>
          <p id="fragAlertText" class="text-xs text-amber-200/90 mt-0.5"></p>
        </div>
      </div>
      <button onclick="apiAction('/api/compact')" class="shrink-0 text-xs font-semibold bg-amber-500 hover:bg-amber-400 text-slate-950 px-4 py-2.5 rounded-lg shadow-md transition flex items-center space-x-2">
        <i data-lucide="shrink" class="w-4 h-4"></i>
        <span>Run C++ Compaction</span>
      </button>
    </div>

    <!-- Metrics Grid -->
    <section class="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-3 sm:gap-4">
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col justify-between shadow">
        <span class="text-xs text-slate-400 font-medium">Total Capacity</span>
        <div class="mt-2 flex items-baseline justify-between">
          <span id="metricTotal" class="text-2xl font-bold font-mono text-white">1000</span>
          <span class="text-xs font-mono text-slate-500">KB</span>
        </div>
        <div class="w-full bg-slate-800 h-1 rounded-full mt-2 overflow-hidden">
          <div class="bg-indigo-500 h-full w-full"></div>
        </div>
      </div>

      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col justify-between shadow">
        <span class="text-xs text-slate-400 font-medium">Allocated RAM</span>
        <div class="mt-2 flex items-baseline justify-between">
          <span id="metricUsed" class="text-2xl font-bold font-mono text-indigo-400">0</span>
          <span id="metricUsedPct" class="text-xs font-mono text-indigo-400/80">0%</span>
        </div>
        <div class="w-full bg-slate-800 h-1 rounded-full mt-2 overflow-hidden">
          <div id="barUsed" class="bg-indigo-500 h-full w-0 transition-all duration-300"></div>
        </div>
      </div>

      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col justify-between shadow">
        <span class="text-xs text-slate-400 font-medium">Total Free RAM</span>
        <div class="mt-2 flex items-baseline justify-between">
          <span id="metricFree" class="text-2xl font-bold font-mono text-emerald-400">1000</span>
          <span id="metricFreePct" class="text-xs font-mono text-emerald-400/80">100%</span>
        </div>
        <div class="w-full bg-slate-800 h-1 rounded-full mt-2 overflow-hidden">
          <div id="barFree" class="bg-emerald-500 h-full w-full transition-all duration-300"></div>
        </div>
      </div>

      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col justify-between shadow">
        <span class="text-xs text-slate-400 font-medium">Max Free Block</span>
        <div class="mt-2 flex items-baseline justify-between">
          <span id="metricMaxContig" class="text-2xl font-bold font-mono text-cyan-400">1000</span>
          <span class="text-xs font-mono text-slate-500">KB</span>
        </div>
        <div class="text-[10px] text-slate-500 font-mono mt-2">Largest contiguous hole</div>
      </div>

      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col justify-between shadow">
        <span class="text-xs text-slate-400 font-medium">C++ DLL Nodes</span>
        <div class="mt-2 flex items-baseline justify-between">
          <span id="metricBlocks" class="text-2xl font-bold font-mono text-slate-200">1</span>
          <span id="metricBlocksSub" class="text-[10px] font-mono text-slate-500">1 Free</span>
        </div>
        <div class="text-[10px] text-slate-500 font-mono mt-2">Block* prev / next</div>
      </div>

      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 flex flex-col justify-between shadow">
        <span class="text-xs text-slate-400 font-medium">std::queue Size</span>
        <div class="mt-2 flex items-baseline justify-between">
          <span id="metricQueue" class="text-2xl font-bold font-mono text-amber-400">0</span>
          <span class="text-xs font-mono text-slate-500">Procs</span>
        </div>
        <div class="text-[10px] text-slate-500 font-mono mt-2">Waiting requests</div>
      </div>
    </section>

    <!-- Visual Memory Map -->
    <section class="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-lg space-y-3">
      <div class="flex flex-col sm:flex-row sm:items-center justify-between pb-3 border-b border-slate-800 gap-3">
        <div>
          <h2 class="text-base font-bold text-white flex items-center gap-2">
            <i data-lucide="layers" class="w-4 h-4 text-indigo-400"></i>
            Interactive Physical Memory Space Map
          </h2>
          <p class="text-xs text-slate-400 mt-0.5">Live view of C++ Doubly Linked List blocks. Click any allocated block to send a FREE request to C++.</p>
        </div>

        <div class="flex items-center space-x-4 text-xs font-mono">
          <div class="flex items-center space-x-1.5">
            <span class="w-3 h-3 rounded bg-indigo-600 border border-indigo-400"></span>
            <span class="text-slate-300">Allocated Process</span>
          </div>
          <div class="flex items-center space-x-1.5">
            <span class="w-3 h-3 rounded bg-slate-900 bg-stripes-slate border border-slate-700"></span>
            <span class="text-slate-300">Free Block</span>
          </div>
        </div>
      </div>

      <div class="flex justify-between text-[11px] font-mono text-slate-500 px-1">
        <span>0 KB</span>
        <span id="scaleMid">500 KB</span>
        <span id="scaleEnd">999 KB</span>
      </div>

      <div id="memoryBar" class="w-full h-20 bg-slate-950 border border-slate-800 rounded-xl p-1 flex items-stretch gap-1 overflow-x-auto shadow-inner relative"></div>

      <div id="blockHoverInfo" class="min-h-[24px] text-xs font-mono text-slate-400 flex items-center justify-between px-1">
        <span class="text-slate-500 italic">Hover over any memory block above to inspect C++ node address bounds...</span>
      </div>
    </section>

    <!-- Controls & CLI -->
    <div class="grid grid-cols-1 lg:grid-cols-12 gap-6">
      <div class="lg:col-span-7 space-y-6">
        <div class="bg-slate-900 border border-slate-800 rounded-xl p-5 space-y-5 shadow">
          <div class="flex items-center justify-between pb-3 border-b border-slate-800">
            <h3 class="text-sm font-bold text-white flex items-center gap-2">
              <i data-lucide="sliders" class="w-4 h-4 text-indigo-400"></i>
              Memory Allocation Control Center
            </h3>

            <div class="flex items-center space-x-2">
              <label for="strategySelect" class="text-xs font-medium text-slate-400">Strategy:</label>
              <select id="strategySelect" onchange="apiAction('/api/strategy?type=' + this.value)" class="bg-slate-950 border border-slate-700 text-slate-200 text-xs font-semibold rounded-lg px-2.5 py-1.5 focus:outline-none focus:border-indigo-500">
                <option value="FIRST_FIT">First Fit</option>
                <option value="BEST_FIT">Best Fit</option>
              </select>
            </div>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <!-- ALLOC -->
            <div class="bg-slate-950/70 border border-slate-800 rounded-xl p-4 space-y-3">
              <div class="flex items-center space-x-2 text-xs font-bold text-indigo-300">
                <i data-lucide="plus-circle" class="w-4 h-4 text-indigo-400"></i>
                <span>ALLOCATE MEMORY</span>
              </div>
              <div class="space-y-2">
                <div>
                  <label class="block text-[11px] font-medium text-slate-400 mb-1">Process ID (PID)</label>
                  <input type="text" id="allocPid" value="P1" placeholder="e.g. P1" class="w-full bg-slate-900 border border-slate-700 rounded-lg px-3 py-1.5 text-xs text-white placeholder-slate-600 focus:outline-none focus:border-indigo-500 uppercase font-mono">
                </div>
                <div>
                  <label class="block text-[11px] font-medium text-slate-400 mb-1">Size (KB)</label>
                  <input type="number" id="allocSize" value="200" min="1" placeholder="e.g. 200" class="w-full bg-slate-900 border border-slate-700 rounded-lg px-3 py-1.5 text-xs text-white placeholder-slate-600 focus:outline-none focus:border-indigo-500 font-mono">
                </div>
                <button onclick="handleAllocSubmit()" class="w-full bg-indigo-600 hover:bg-indigo-500 text-white font-semibold text-xs py-2 rounded-lg transition shadow-md shadow-indigo-600/20 flex items-center justify-center space-x-1.5">
                  <i data-lucide="check-circle" class="w-3.5 h-3.5"></i>
                  <span>Allocate Block</span>
                </button>
              </div>
            </div>

            <!-- FREE -->
            <div class="bg-slate-950/70 border border-slate-800 rounded-xl p-4 space-y-3 flex flex-col justify-between">
              <div class="space-y-3">
                <div class="flex items-center space-x-2 text-xs font-bold text-rose-300">
                  <i data-lucide="minus-circle" class="w-4 h-4 text-rose-400"></i>
                  <span>FREE PROCESS</span>
                </div>
                <div>
                  <label class="block text-[11px] font-medium text-slate-400 mb-1">Select Active Process</label>
                  <select id="freePidSelect" class="w-full bg-slate-900 border border-slate-700 rounded-lg px-3 py-1.5 text-xs text-white focus:outline-none focus:border-rose-500 font-mono">
                    <option value="">-- No Active Processes --</option>
                  </select>
                </div>
              </div>
              <button onclick="handleFreeSubmit()" class="w-full bg-slate-800 hover:bg-rose-950/80 hover:text-rose-200 border border-slate-700 hover:border-rose-700 text-slate-300 font-semibold text-xs py-2 rounded-lg transition flex items-center justify-center space-x-1.5">
                <i data-lucide="trash-2" class="w-3.5 h-3.5"></i>
                <span>Release Memory</span>
              </button>
            </div>
          </div>

          <div class="pt-3 border-t border-slate-800 flex flex-wrap items-center justify-between gap-3">
            <div class="flex items-center space-x-2">
              <button onclick="apiAction('/api/compact')" class="bg-amber-600/20 hover:bg-amber-600/30 border border-amber-500/40 text-amber-200 font-semibold text-xs px-3.5 py-2 rounded-lg transition flex items-center space-x-2">
                <i data-lucide="shrink" class="w-4 h-4 text-amber-400"></i>
                <span>Compact Memory</span>
              </button>
              <button onclick="openResetModal()" class="bg-slate-800 hover:bg-slate-700 border border-slate-700 text-slate-300 text-xs font-semibold px-3.5 py-2 rounded-lg transition">
                Clear All
              </button>
            </div>
            <div class="text-xs text-slate-400 font-mono">
              Coalescing: <span class="text-emerald-400 font-bold">AUTOMATIC (C++)</span>
            </div>
          </div>
        </div>

        <!-- Scenario Presets -->
        <div class="bg-slate-900 border border-slate-800 rounded-xl p-5 space-y-3 shadow">
          <div class="flex items-center justify-between pb-2 border-b border-slate-800">
            <h3 class="text-sm font-bold text-white flex items-center gap-2">
              <i data-lucide="play-circle" class="w-4 h-4 text-indigo-400"></i>
              C++ Scenario Presets
            </h3>
            <span class="text-[11px] text-slate-400">Execute preset workloads in C++</span>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-3 gap-3">
            <button onclick="apiAction('/api/preset?name=frag')" class="p-3 bg-slate-950 hover:bg-slate-850 border border-slate-800 hover:border-amber-500/40 rounded-xl text-left transition space-y-1 group">
              <div class="text-xs font-bold text-amber-300 group-hover:text-amber-200 flex items-center gap-1.5">
                <i data-lucide="alert-circle" class="w-3.5 h-3.5"></i>
                <span>External Frag Test</span>
              </div>
              <p class="text-[11px] text-slate-400 leading-normal">Fragments free blocks and stalls P5 to trigger compaction alert.</p>
            </button>

            <button onclick="apiAction('/api/preset?name=strategy')" class="p-3 bg-slate-950 hover:bg-slate-850 border border-slate-800 hover:border-indigo-500/40 rounded-xl text-left transition space-y-1 group">
              <div class="text-xs font-bold text-indigo-300 group-hover:text-indigo-200 flex items-center gap-1.5">
                <i data-lucide="git-compare" class="w-3.5 h-3.5"></i>
                <span>First vs Best Fit</span>
              </div>
              <p class="text-[11px] text-slate-400 leading-normal">Creates 300 KB and 200 KB holes to compare allocation algorithms.</p>
            </button>

            <button onclick="apiAction('/api/preset?name=heavy')" class="p-3 bg-slate-950 hover:bg-slate-850 border border-slate-800 hover:border-violet-500/40 rounded-xl text-left transition space-y-1 group">
              <div class="text-xs font-bold text-violet-300 group-hover:text-violet-200 flex items-center gap-1.5">
                <i data-lucide="zap" class="w-3.5 h-3.5"></i>
                <span>Heavy Workload</span>
              </div>
              <p class="text-[11px] text-slate-400 leading-normal">Fills 900 KB of RAM and pushes overflow into std::queue.</p>
            </button>
          </div>
        </div>
      </div>

      <!-- C++ CLI Terminal -->
      <div class="lg:col-span-5 flex flex-col space-y-6">
        <div class="bg-slate-900 border border-slate-800 rounded-xl p-5 flex-1 flex flex-col justify-between shadow-lg min-h-[440px]">
          <div class="flex items-center justify-between pb-3 border-b border-slate-800">
            <div class="flex items-center space-x-2">
              <i data-lucide="terminal" class="w-4 h-4 text-emerald-400"></i>
              <h3 class="text-sm font-bold text-white">C++ Backend Execution Shell</h3>
            </div>
            <button onclick="apiAction('/api/clearlogs')" class="text-[11px] text-slate-400 hover:text-slate-200 hover:underline">
              Clear Log
            </button>
          </div>

          <div id="terminalLog" class="my-3 p-3 bg-slate-950 border border-slate-850 rounded-lg font-mono text-[11px] space-y-1.5 overflow-y-auto max-h-[340px] flex-1"></div>

          <form onsubmit="handleCliCommand(event)" class="mt-2 flex items-center space-x-2">
            <span class="text-emerald-400 font-mono text-xs font-bold">vmm&gt;</span>
            <input type="text" id="cliInput" placeholder="ALLOC P1 200 | FREE P1 | COMPACT | STRATEGY BEST" class="flex-1 bg-slate-950 border border-slate-700 rounded-lg px-3 py-1.5 text-xs text-emerald-300 placeholder-slate-600 font-mono focus:outline-none focus:border-emerald-500">
            <button type="submit" class="bg-slate-800 hover:bg-slate-700 border border-slate-700 text-slate-200 text-xs px-3 py-1.5 rounded-lg font-mono">
              Run
            </button>
          </form>
        </div>
      </div>
    </div>

    <!-- Data Structures Visualizer -->
    <section class="bg-slate-900 border border-slate-800 rounded-xl p-5 space-y-4 shadow-lg">
      <div class="flex flex-col sm:flex-row sm:items-center justify-between pb-3 border-b border-slate-800 gap-3">
        <div>
          <h3 class="text-base font-bold text-white flex items-center gap-2">
            <i data-lucide="network" class="w-4 h-4 text-indigo-400"></i>
            Live C++ Data Structures Inspector
          </h3>
          <p class="text-xs text-slate-400 mt-0.5">Directly serialized from C++ memory: Doubly Linked List, std::unordered_map, and std::queue.</p>
        </div>

        <div class="flex items-center space-x-1 bg-slate-950 p-1 rounded-lg border border-slate-800 text-xs font-medium">
          <button id="dsTabBtnDll" onclick="switchDsTab('dll')" class="px-3 py-1.5 rounded-md text-slate-200 bg-slate-800 transition">
            Doubly Linked List
          </button>
          <button id="dsTabBtnHash" onclick="switchDsTab('hash')" class="px-3 py-1.5 rounded-md text-slate-400 hover:text-slate-200 transition">
            std::unordered_map
          </button>
          <button id="dsTabBtnQueue" onclick="switchDsTab('queue')" class="px-3 py-1.5 rounded-md text-slate-400 hover:text-slate-200 transition flex items-center space-x-1.5">
            <span>std::queue</span>
            <span id="queueTabBadge" class="bg-amber-500/20 text-amber-300 text-[10px] px-1.5 py-0.2 rounded-full font-mono">0</span>
          </button>
        </div>
      </div>

      <div id="dsTabContent" class="min-h-[160px]"></div>
    </section>
  </main>

  <!-- Reset Modal -->
  <div id="resetModal" class="hidden fixed inset-0 z-50 bg-slate-950/80 backdrop-blur-sm flex items-center justify-center p-4">
    <div class="bg-slate-900 border border-slate-800 rounded-2xl max-w-md w-full p-6 space-y-4 shadow-2xl">
      <div class="flex items-center justify-between pb-3 border-b border-slate-800">
        <h3 class="text-base font-bold text-white flex items-center gap-2">
          <i data-lucide="rotate-ccw" class="w-5 h-5 text-indigo-400"></i>
          Reset C++ Memory Manager
        </h3>
        <button onclick="closeResetModal()" class="text-slate-400 hover:text-white">
          <i data-lucide="x" class="w-5 h-5"></i>
        </button>
      </div>
      <p class="text-xs text-slate-300">
        Deallocates all C++ linked list nodes, clears the hash table and waiting queue, and initializes a new contiguous RAM block.
      </p>
      <div>
        <label class="block text-xs font-semibold text-slate-300 mb-1">Total Memory Capacity (KB)</label>
        <input type="number" id="newMemSize" value="1000" min="100" max="100000" step="50" class="w-full bg-slate-950 border border-slate-700 rounded-lg px-3 py-2 text-sm text-white font-mono focus:outline-none focus:border-indigo-500">
      </div>
      <div class="flex items-center justify-end space-x-3 pt-2">
        <button onclick="closeResetModal()" class="px-4 py-2 rounded-lg text-xs font-semibold text-slate-400 hover:bg-slate-800">Cancel</button>
        <button onclick="confirmResetMemory()" class="px-4 py-2 rounded-lg text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white shadow-md">Reset System</button>
      </div>
    </div>
  </div>

  <footer class="border-t border-slate-800 bg-slate-900/60 py-4 mt-8">
    <div class="max-w-7xl mx-auto px-4 text-center text-xs text-slate-500 font-mono">
      Virtual Memory Manager &bull; Full-Stack C++11 Socket Server + Interactive OS Simulation UI
    </div>
  </footer>

  <script>
    const PROCESS_COLORS = [
      { bg: 'bg-indigo-600', border: 'border-indigo-400', text: 'text-white' },
      { bg: 'bg-violet-600', border: 'border-violet-400', text: 'text-white' },
      { bg: 'bg-cyan-600', border: 'border-cyan-400', text: 'text-white' },
      { bg: 'bg-emerald-600', border: 'border-emerald-400', text: 'text-white' },
      { bg: 'bg-amber-600', border: 'border-amber-400', text: 'text-white' },
      { bg: 'bg-rose-600', border: 'border-rose-400', text: 'text-white' },
      { bg: 'bg-sky-600', border: 'border-sky-400', text: 'text-white' },
      { bg: 'bg-fuchsia-600', border: 'border-fuchsia-400', text: 'text-white' }
    ];

    function getProcessColor(pid) {
      let hash = 0;
      for (let i = 0; i < pid.length; i++) {
        hash = pid.charCodeAt(i) + ((hash << 5) - hash);
      }
      return PROCESS_COLORS[Math.abs(hash) % PROCESS_COLORS.length];
    }

    let currentState = null;
    let activeDsTab = 'dll';

    async function apiAction(url) {
      try {
        const res = await fetch(url, { method: 'POST' });
        currentState = await res.json();
        renderAll();
      } catch (err) {
        console.error('C++ API error:', err);
      }
    }

    async function fetchInitialState() {
      try {
        const res = await fetch('/api/state');
        currentState = await res.json();
        renderAll();
      } catch (err) {
        console.error('Failed to connect to C++ server:', err);
      }
    }

    function renderAll() {
      if (!currentState) return;
      const s = currentState;

      // Metrics
      const usedPct = Math.round((s.usedMemory / s.totalMemory) * 100);
      const freePct = 100 - usedPct;

      document.getElementById('metricTotal').innerText = s.totalMemory;
      document.getElementById('metricUsed').innerText = s.usedMemory;
      document.getElementById('metricUsedPct').innerText = `${usedPct}%`;
      document.getElementById('barUsed').style.width = `${usedPct}%`;

      document.getElementById('metricFree').innerText = s.freeMemory;
      document.getElementById('metricFreePct').innerText = `${freePct}%`;
      document.getElementById('barFree').style.width = `${freePct}%`;

      document.getElementById('metricMaxContig').innerText = s.maxContiguous;
      document.getElementById('metricBlocks').innerText = s.nodeCount;
      document.getElementById('metricBlocksSub').innerText = `${s.freeNodeCount} Free`;
      document.getElementById('metricQueue').innerText = s.queue.length;
      document.getElementById('queueTabBadge').innerText = s.queue.length;

      document.getElementById('scaleMid').innerText = `${Math.round(s.totalMemory / 2)} KB`;
      document.getElementById('scaleEnd').innerText = `${s.totalMemory - 1} KB`;
      document.getElementById('activeStrategyBadge').innerText = s.strategy.replace('_', ' ');
      document.getElementById('strategySelect').value = s.strategy;

      // Fragmentation banner
      const alertDiv = document.getElementById('fragAlert');
      if (s.fragmentation && s.fragmentation.detected) {
        document.getElementById('fragAlertText').innerText =
          `Process ${s.fragmentation.pid} requires ${s.fragmentation.requestedSize} KB. Total free RAM is ${s.freeMemory} KB, but largest contiguous C++ free block is only ${s.maxContiguous} KB.`;
        alertDiv.classList.remove('hidden');
      } else {
        alertDiv.classList.add('hidden');
      }

      // Memory bar
      const bar = document.getElementById('memoryBar');
      bar.innerHTML = '';
      s.blocks.forEach(node => {
        const pct = (node.size / s.totalMemory) * 100;
        const div = document.createElement('div');
        div.style.width = `${pct}%`;
        div.className = 'h-full rounded-lg transition-all duration-300 relative flex flex-col justify-center items-center text-center px-1 overflow-hidden cursor-pointer border select-none shrink-0';

        if (node.isFree) {
          div.className += ' bg-slate-900 bg-stripes-slate border-slate-800 text-slate-500 hover:border-slate-600';
          div.innerHTML = `
            <span class="text-[10px] font-mono font-semibold truncate ${pct < 4 ? 'hidden' : ''}">FREE</span>
            <span class="text-[9px] font-mono opacity-75 truncate ${pct < 8 ? 'hidden' : ''}">${node.size} KB</span>
          `;
        } else {
          const c = getProcessColor(node.pid);
          div.className += ` ${c.bg} ${c.border} ${c.text} shadow-md hover:brightness-110`;
          div.onclick = () => apiAction('/api/free?pid=' + encodeURIComponent(node.pid));
          div.innerHTML = `
            <span class="text-xs font-mono font-bold truncate ${pct < 3 ? 'hidden' : ''}">${node.pid}</span>
            <span class="text-[9px] font-mono opacity-90 truncate ${pct < 7 ? 'hidden' : ''}">${node.size} KB</span>
          `;
        }

        div.onmouseenter = () => {
          document.getElementById('blockHoverInfo').innerHTML = `
            <span class="text-slate-200 font-semibold">${node.isFree ? 'FREE BLOCK' : 'PROCESS ' + node.pid} (Node #${node.id})</span>
            <span>Range: <strong class="text-indigo-300">[${node.start} - ${node.end}]</strong></span>
            <span>Size: <strong class="text-cyan-300">${node.size} KB</strong> (${pct.toFixed(1)}%)</span>
            <span class="text-slate-400 font-sans">${node.isFree ? '(Free Segment)' : '(Click to Release in C++)'}</span>
          `;
        };
        div.onmouseleave = () => {
          document.getElementById('blockHoverInfo').innerHTML =
            `<span class="text-slate-500 italic">Hover over any memory block above to inspect C++ node address bounds...</span>`;
        };
        bar.appendChild(div);
      });

      // Dropdown
      const select = document.getElementById('freePidSelect');
      const prev = select.value;
      select.innerHTML = '<option value="">-- Select Active Process --</option>';
      s.hashTable.forEach(item => {
        const opt = document.createElement('option');
        opt.value = item.pid;
        opt.innerText = `${item.pid} (${item.size} KB @ [${item.start} - ${item.end}])`;
        select.appendChild(opt);
      });
      if (s.hashTable.some(i => i.pid === prev)) select.value = prev;

      // Logs
      const term = document.getElementById('terminalLog');
      term.innerHTML = '';
      s.logs.forEach(entry => {
        let tagClass = 'text-slate-400';
        if (entry.type === 'SUCCESS') tagClass = 'text-emerald-400';
        else if (entry.type === 'ERROR') tagClass = 'text-rose-400';
        else if (entry.type === 'WARNING') tagClass = 'text-amber-400';
        else if (entry.type === 'FRAG') tagClass = 'text-amber-300 font-bold';
        else if (entry.type === 'COMPACT') tagClass = 'text-cyan-400 font-bold';
        else if (entry.type === 'QUEUE') tagClass = 'text-violet-400';

        const row = document.createElement('div');
        row.className = 'leading-relaxed';
        row.innerHTML = `<span class="text-slate-600">${entry.time}</span> <span class="${tagClass}">[${entry.type}]</span> ${escapeHtml(entry.message)}`;
        term.appendChild(row);
      });
      term.scrollTop = term.scrollHeight;

      renderDsTab();
      lucide.createIcons();
    }

    function switchDsTab(tab) {
      activeDsTab = tab;
      document.getElementById('dsTabBtnDll').className = tab === 'dll' ? 'px-3 py-1.5 rounded-md text-slate-200 bg-slate-800 transition' : 'px-3 py-1.5 rounded-md text-slate-400 hover:text-slate-200 transition';
      document.getElementById('dsTabBtnHash').className = tab === 'hash' ? 'px-3 py-1.5 rounded-md text-slate-200 bg-slate-800 transition' : 'px-3 py-1.5 rounded-md text-slate-400 hover:text-slate-200 transition';
      document.getElementById('dsTabBtnQueue').className = tab === 'queue' ? 'px-3 py-1.5 rounded-md text-slate-200 bg-slate-800 transition flex items-center space-x-1.5' : 'px-3 py-1.5 rounded-md text-slate-400 hover:text-slate-200 transition flex items-center space-x-1.5';
      renderDsTab();
      lucide.createIcons();
    }

    function renderDsTab() {
      if (!currentState) return;
      const container = document.getElementById('dsTabContent');

      if (activeDsTab === 'dll') {
        let html = `<div class="flex items-center gap-3 overflow-x-auto py-3 min-h-[140px]">`;
        currentState.blocks.forEach((b, i) => {
          const prevTxt = b.prevId !== -1 ? `Node #${b.prevId}` : 'nullptr (HEAD)';
          const nextTxt = b.nextId !== -1 ? `Node #${b.nextId}` : 'nullptr (TAIL)';
          const badge = b.isFree
            ? `<span class="px-2 py-0.5 rounded text-[10px] bg-slate-800 text-slate-400 border border-slate-700">FREE</span>`
            : `<span class="px-2 py-0.5 rounded text-[10px] bg-indigo-950 text-indigo-300 border border-indigo-700 font-bold">${b.pid}</span>`;

          html += `
            <div class="bg-slate-950 border border-slate-800 hover:border-slate-700 rounded-xl p-3 min-w-[215px] shrink-0 font-mono text-xs space-y-2 shadow">
              <div class="flex items-center justify-between pb-1.5 border-b border-slate-850">
                <span class="text-slate-400 text-[11px]">Block #${b.id}</span>
                ${badge}
              </div>
              <div class="space-y-1 text-[11px]">
                <div class="flex justify-between"><span class="text-slate-500">Address:</span> <span class="text-slate-200">[${b.start} - ${b.end}]</span></div>
                <div class="flex justify-between"><span class="text-slate-500">Size:</span> <span class="text-cyan-400 font-bold">${b.size} KB</span></div>
              </div>
              <div class="pt-1 border-t border-slate-850 text-[10px] flex justify-between text-slate-500">
                <span>prev: <strong class="text-slate-400">${prevTxt}</strong></span>
                <span>next: <strong class="text-slate-400">${nextTxt}</strong></span>
              </div>
            </div>
          `;
          if (i < currentState.blocks.length - 1) {
            html += `<div class="text-slate-600 font-mono text-xs font-bold shrink-0">&lt;---&gt;</div>`;
          }
        });
        html += `</div>`;
        container.innerHTML = html;
      } else if (activeDsTab === 'hash') {
        if (currentState.hashTable.length === 0) {
          container.innerHTML = `<div class="py-6 text-center text-xs text-slate-500 italic font-mono">std::unordered_map&lt;string, Block*&gt; is empty.</div>`;
          return;
        }
        let html = `
          <div class="overflow-x-auto">
            <table class="w-full text-left border-collapse text-xs font-mono">
              <thead>
                <tr class="border-b border-slate-800 text-slate-400 bg-slate-950/50">
                  <th class="py-2 px-3">Key (PID)</th>
                  <th class="py-2 px-3">Mapped Pointer</th>
                  <th class="py-2 px-3">Address Range</th>
                  <th class="py-2 px-3">Size</th>
                  <th class="py-2 px-3 text-right">Action</th>
                </tr>
              </thead>
              <tbody class="divide-y divide-slate-800/60 text-slate-300">
        `;
        currentState.hashTable.forEach(item => {
          html += `
            <tr class="hover:bg-slate-800/30">
              <td class="py-2 px-3 font-bold text-indigo-400">${item.pid}</td>
              <td class="py-2 px-3 text-slate-400">Block* (#${item.nodeId})</td>
              <td class="py-2 px-3">[${item.start} - ${item.end}]</td>
              <td class="py-2 px-3 text-cyan-400">${item.size} KB</td>
              <td class="py-2 px-3 text-right">
                <button onclick="apiAction('/api/free?pid=${encodeURIComponent(item.pid)}')" class="text-xs text-rose-400 hover:underline font-sans">Free</button>
              </td>
            </tr>
          `;
        });
        html += `</tbody></table></div>`;
        container.innerHTML = html;
      } else if (activeDsTab === 'queue') {
        if (currentState.queue.length === 0) {
          container.innerHTML = `<div class="py-6 text-center text-xs text-slate-500 italic font-mono">std::queue&lt;ProcessRequest&gt; is empty.</div>`;
          return;
        }
        let html = `<div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-3 py-2">`;
        currentState.queue.forEach((item, idx) => {
          html += `
            <div class="bg-slate-950 border border-amber-500/30 rounded-xl p-3 flex items-center justify-between font-mono text-xs shadow-sm">
              <div class="space-y-0.5">
                <div class="flex items-center space-x-2">
                  <span class="text-amber-400 font-bold">#${idx + 1}</span>
                  <span class="text-white font-bold">${item.pid}</span>
                </div>
                <div class="text-[11px] text-slate-400">Requested: <span class="text-amber-300">${item.size} KB</span></div>
              </div>
              <button onclick="apiAction('/api/cancelqueue?index=${idx}')" class="text-slate-500 hover:text-rose-400 p-1" title="Remove from Queue">
                <i data-lucide="x-circle" class="w-4 h-4"></i>
              </button>
            </div>
          `;
        });
        html += `</div>`;
        container.innerHTML = html;
      }
    }

    function escapeHtml(str) {
      return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    async function handleAllocSubmit() {
      const pidInput = document.getElementById('allocPid');
      const sizeInput = document.getElementById('allocSize');
      const pid = pidInput.value.trim();
      const size = parseInt(sizeInput.value, 10);
      if (!pid || isNaN(size)) return;

      await apiAction(`/api/alloc?pid=${encodeURIComponent(pid)}&size=${size}`);
      const match = pid.match(/^P(\d+)$/i);
      if (match) pidInput.value = `P${parseInt(match[1], 10) + 1}`;
    }

    function handleFreeSubmit() {
      const pid = document.getElementById('freePidSelect').value;
      if (pid) apiAction(`/api/free?pid=${encodeURIComponent(pid)}`);
    }

    function handleCliCommand(e) {
      e.preventDefault();
      const input = document.getElementById('cliInput');
      const raw = input.value.trim();
      if (!raw) return;
      input.value = '';

      const parts = raw.split(/\s+/);
      const cmd = parts[0].toUpperCase();

      if ((cmd === 'ALLOC' || cmd === 'ALLOCATE') && parts.length >= 3) {
        apiAction(`/api/alloc?pid=${encodeURIComponent(parts[1])}&size=${parseInt(parts[2], 10)}`);
      } else if (cmd === 'FREE' && parts.length >= 2) {
        apiAction(`/api/free?pid=${encodeURIComponent(parts[1])}`);
      } else if (cmd === 'COMPACT') {
        apiAction('/api/compact');
      } else if (cmd === 'STRATEGY' && parts[1]) {
        const s = parts[1].toUpperCase();
        if (s === 'FIRST' || s === 'FIRST_FIT') apiAction('/api/strategy?type=FIRST_FIT');
        else if (s === 'BEST' || s === 'BEST_FIT') apiAction('/api/strategy?type=BEST_FIT');
      } else if (cmd === 'CLEAR') {
        apiAction('/api/clearlogs');
      } else {
        apiAction(`/api/cli?cmd=${encodeURIComponent(raw)}`);
      }
    }

    function openResetModal() { document.getElementById('resetModal').classList.remove('hidden'); }
    function closeResetModal() { document.getElementById('resetModal').classList.add('hidden'); }
    function confirmResetMemory() {
      const val = parseInt(document.getElementById('newMemSize').value, 10);
      if (val >= 100) {
        closeResetModal();
        apiAction(`/api/reset?size=${val}`);
      }
    }

    window.onload = fetchInitialState;
  </script>
</body>
</html>)HTML";

// ============================================================================
// 3. HTTP REQUEST PARSING & LIGHTWEIGHT C++ SOCKET SERVER
// ============================================================================

std::string urlDecode(const std::string& str) {
    std::string ret;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int hex_val = 0;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> hex_val) {
                ret += static_cast<char>(hex_val);
                i += 2;
            }
        } else if (str[i] == '+') {
            ret += ' ';
        } else {
            ret += str[i];
        }
    }
    return ret;
}

std::string getQueryParam(const std::string& query, const std::string& key) {
    std::string search = key + "=";
    size_t pos = query.find(search);
    if (pos == std::string::npos) return "";
    size_t start = pos + search.length();
    size_t end = query.find('&', start);
    std::string raw = (end == std::string::npos) ? query.substr(start) : query.substr(start, end - start);
    return urlDecode(raw);
}

std::string buildHttpResponse(const std::string& status, const std::string& content_type, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << content_type << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return oss.str();
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    // Allow port override via PORT environment variable (useful for cloud hosting)
    int port = 8080;
    const char* env_port = std::getenv("PORT");
    if (env_port != nullptr) {
        int parsed = std::atoi(env_port);
        if (parsed > 0) port = parsed;
    }

    SocketType server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCK) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Bind failed on port " << port << ".\n";
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "Listen failed.\n";
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    VirtualMemoryManager vmm(1000);

    std::cout << "=================================================================\n";
    std::cout << "  VIRTUAL MEMORY MANAGER (C++ WEB SERVER) RUNNING ON PORT " << port << "\n";
    std::cout << "  Open in browser: http://localhost:" << port << "\n";
    std::cout << "=================================================================\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        SocketType client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        if (client_fd == INVALID_SOCK) continue;

        char buffer[8192];
        std::memset(buffer, 0, sizeof(buffer));
        int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read > 0) {
            std::string request(buffer, bytes_read);
            std::istringstream req_stream(request);
            std::string method, full_url, version;
            req_stream >> method >> full_url >> version;

            std::string path = full_url;
            std::string query = "";
            size_t q_pos = full_url.find('?');
            if (q_pos != std::string::npos) {
                path = full_url.substr(0, q_pos);
                query = full_url.substr(q_pos + 1);
            }

            std::string response;

            if (path == "/" || path == "/index.html") {
                response = buildHttpResponse("200 OK", "text/html", INDEX_HTML);
            } else if (path == "/api/state") {
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/alloc") {
                std::string pid = getQueryParam(query, "pid");
                int size = std::atoi(getQueryParam(query, "size").c_str());
                vmm.allocate(pid, size);
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/free") {
                std::string pid = getQueryParam(query, "pid");
                vmm.freeMemory(pid);
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/compact") {
                vmm.compact();
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/strategy") {
                std::string type = getQueryParam(query, "type");
                if (type == "BEST_FIT") vmm.setStrategy(BEST_FIT);
                else vmm.setStrategy(FIRST_FIT);
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/reset") {
                int size = std::atoi(getQueryParam(query, "size").c_str());
                vmm.reset(size > 0 ? size : 1000);
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/preset") {
                std::string name = getQueryParam(query, "name");
                vmm.runPreset(name);
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/cancelqueue") {
                int idx = std::atoi(getQueryParam(query, "index").c_str());
                vmm.cancelQueueItem(idx);
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/clearlogs") {
                vmm.clearLogs();
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else if (path == "/api/cli") {
                std::string cmd = getQueryParam(query, "cmd");
                vmm.addLog("INFO", "Supported CLI commands: ALLOC <PID> <SIZE>, FREE <PID>, COMPACT, STRATEGY <FIRST|BEST>, CLEAR");
                response = buildHttpResponse("200 OK", "application/json", vmm.toJson());
            } else {
                response = buildHttpResponse("404 Not Found", "text/plain", "404 Not Found");
            }

            send(client_fd, response.c_str(), static_cast<int>(response.size()), 0);
        }

        CLOSE_SOCKET(client_fd);
    }

    CLOSE_SOCKET(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}