
#include <assert.h>

#include "triage_onchip.h"
#include "triage.h"

using namespace std;

//#define DEBUG

#ifdef DEBUG
#define debug_cout cerr << "[TRIAGE_ONCHIP] "
#else
#define debug_cout if (0) cerr
#endif

TriageOnchipEntry::TriageOnchipEntry() {
    init();
}

void TriageOnchipEntry::init() {
    for (uint32_t i = 0; i < ONCHIP_LINE_SIZE; i++) {
        metadata[i].spatial = false;
        metadata[i].addr = INVALID_ADDR;
        confidence[i] = 3;
        valid[i] = false;
    }
}

void TriageOnchipEntry::increase_confidence(uint32_t offset) {
    if (confidence[offset] < 3)
        confidence[offset]++;
}

void TriageOnchipEntry::decrease_confidence(uint32_t offset) {
    if (confidence[offset] > 0)
        confidence[offset]--;
}

TriageOnchip::TriageOnchip() {}

void TriageOnchip::set_conf(TriageConfig *config) {
    assoc = config->on_chip_assoc;
    num_sets = config->on_chip_set;
    num_sets = num_sets >> ONCHIP_LINE_SHIFT;
    repl_type = config->repl;
    index_mask = num_sets - 1;
    use_dynamic_assoc = config->use_dynamic_assoc;

    entry_list.resize(num_sets);
    repl = TriageRepl::create_repl(&entry_list, repl_type, assoc, use_dynamic_assoc);
    cout << "Num Sets: " << num_sets << endl;
}

uint64_t TriageOnchip::get_line_offset(uint64_t addr) {
    uint64_t line_offset = addr & (ONCHIP_LINE_SIZE-1);
    return line_offset;
}


uint64_t TriageOnchip::get_set_id(uint64_t addr) {
    uint64_t set_id = (addr >> ONCHIP_LINE_SHIFT) & index_mask;
    debug_cout << "num_sets: " << num_sets << ", index_mask: " << index_mask << ", set_id: " << set_id <<endl;
    assert(set_id < num_sets);
    return set_id;
}

int TriageOnchip::increase_confidence(uint64_t addr) {
    uint64_t set_id = get_set_id(addr);
    assert(set_id < num_sets);
    uint64_t line_offset = get_line_offset(addr);
    uint64_t tag = addr >> ONCHIP_LINE_SHIFT;
    map<uint64_t, TriageOnchipEntry>& entry_map = entry_list[set_id];
    map<uint64_t, TriageOnchipEntry>::iterator it = entry_map.find(tag);

    it->second.increase_confidence(line_offset);
    return it->second.confidence[line_offset];
}

int TriageOnchip::decrease_confidence(uint64_t addr) {
    uint64_t set_id = get_set_id(addr);
    assert(set_id < num_sets);
    uint64_t line_offset = get_line_offset(addr);
    uint64_t tag = addr >> ONCHIP_LINE_SHIFT;
    map<uint64_t, TriageOnchipEntry>& entry_map = entry_list[set_id];
    map<uint64_t, TriageOnchipEntry>::iterator it = entry_map.find(tag);

    it->second.decrease_confidence(line_offset);
    return it->second.confidence[line_offset];
}

void TriageOnchip::update(uint64_t prev_addr, Metadata next_entry, uint64_t pc, bool update_repl) {
    if (use_dynamic_assoc) {
        assoc = repl->get_assoc();
    }
    uint64_t set_id = get_set_id(prev_addr);
    assert(set_id < num_sets);
    uint64_t line_offset = get_line_offset(prev_addr);
    uint64_t tag = prev_addr >> ONCHIP_LINE_SHIFT;
    map<uint64_t, TriageOnchipEntry>& entry_map = entry_list[set_id];
    map<uint64_t, TriageOnchipEntry>::iterator it = entry_map.find(tag);
    debug_cout << hex << "update prev_addr: " << prev_addr
        << ", set_id: " << set_id
        << ", tag: " << tag
        << ", assoc: " << assoc
        << ", entry map size: " << entry_map.size()
        << ", pc: " << pc
        << endl;
    if (it != entry_map.end()) {
        while (repl_type != TRIAGE_REPL_PERFECT && entry_map.size() > assoc && entry_map.size() > 0) {
            uint64_t victim_addr = repl->pickVictim(set_id);
            assert(entry_map.count(victim_addr));
            entry_map.erase(victim_addr);
        }
        if (assoc > 0) {
            it->second.metadata[line_offset] = next_entry;
            it->second.valid[line_offset] = true;
        }
        if(update_repl)
            repl->addEntry(set_id, tag, pc);
    } else {
        while (repl_type != TRIAGE_REPL_PERFECT && entry_map.size() >= assoc && entry_map.size() > 0) {
            uint64_t victim_addr = repl->pickVictim(set_id);
            assert(entry_map.count(victim_addr));
            entry_map.erase(victim_addr);
        }
        debug_cout << "entry_map_size A: " << entry_map.size() << endl;
        assert(!entry_map.count(tag));
        if (assoc > 0) {
            entry_map[tag].init();
            entry_map[tag].metadata[line_offset] = next_entry;
            entry_map[tag].confidence[line_offset] = 3;
            entry_map[tag].valid[line_offset] = true;
        }
        debug_cout << "entry_map_size B: " << entry_map.size() << endl;
        repl->addEntry(set_id, tag, pc);
    }

    debug_cout << hex << "after update prev_addr: " << prev_addr
        << ", set_id: " << set_id
        << ", tag: " << tag
        << ", assoc: " << assoc
        << ", entry map size: " << entry_map.size()
        << ", pc: " << pc
        << endl;

    assert(entry_map.size() <= assoc);
}

Metadata TriageOnchip::get_next_entry(uint64_t prev_addr, uint64_t pc, bool update_stats) {
    if (use_dynamic_assoc) {
        assoc = repl->get_assoc();
    }
    uint64_t set_id = get_set_id(prev_addr);
    assert(set_id < num_sets);
    uint64_t line_offset = get_line_offset(prev_addr);
    uint64_t tag = prev_addr >> ONCHIP_LINE_SHIFT;
    map<uint64_t, TriageOnchipEntry>& entry_map = entry_list[set_id];
    debug_cout << hex << "get_next_addr prev_addr: " << prev_addr
        << ", set_id: " << set_id
        << ", tag: " << tag
        << ", pc: " << pc
        << endl;

    map<uint64_t, TriageOnchipEntry>::iterator it = entry_map.find(tag);

    Metadata next_entry;
    if (it != entry_map.end() && (it->second.valid[line_offset])) {
        next_entry = it->second.metadata[line_offset];
        if (update_stats) {
            repl->addEntry(set_id, tag, pc);
        }
    }
    return next_entry;
}

uint32_t TriageOnchip::get_assoc()
{
    return assoc;
}

void TriageOnchip::print_stats()
{
    assert(repl != NULL);
    repl->print_stats();
}

