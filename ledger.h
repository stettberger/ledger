#ifndef _LEDGER_H
#define _LEDGER_H "$Revision: 1.19 $"

//////////////////////////////////////////////////////////////////////
//
// ledger: Double-entry ledger accounting
//
//   by John Wiegley <johnw@newartisans.com>
//
// Copyright (c) 2003 New Artisans, Inc.  All Rights Reserved.

#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <ctime>

#ifdef DEBUG
#include <cassert>
#else
#define assert(x)
#endif

#include <pcre.h>               // Perl regular expression library

namespace ledger {

struct commodity
{
  std::string name;
  std::string symbol;

  bool prefix;
  bool separate;
  bool thousands;
  bool european;

  int precision;

  commodity() : prefix(false), separate(true),
       thousands(false), european(false) {}
  commodity(const std::string& sym, bool pre = false, bool sep = true,
	    bool thou = true, bool euro = false, int prec = 2);
};

typedef std::map<const std::string, commodity *>  commodities_map;
typedef commodities_map::iterator                 commodities_map_iterator;
typedef std::pair<const std::string, commodity *> commodities_map_pair;


class amount
{
 public:
  virtual ~amount() {}

  virtual commodity * comm() const = 0;
  virtual const std::string& comm_symbol() const = 0;
  virtual amount * copy() const = 0;
  virtual amount * value(amount * pr = NULL) const = 0;
  virtual amount * street() const = 0;

  virtual bool has_price() const = 0;
  virtual void set_value(const amount * pr) = 0;

  // Test if the quantity is zero

  virtual bool is_zero() const = 0;

  // Assignment

  virtual void credit(const amount * other) = 0;
  virtual void negate() = 0;

  // String conversion routines

  virtual void parse(const std::string& num) = 0;
  virtual const std::string as_str(bool full_prec = false) const = 0;
};

extern amount * create_amount(const std::string& value,
			      const amount * cost = NULL);

struct mask
{
  bool        exclude;
  std::string pattern;
  pcre *      regexp;

  mask(const std::string& pattern);
};

typedef std::list<mask> regexps_map;

void record_regexp(const std::string& pattern, regexps_map& regexps);
void read_regexps(const std::string& path, regexps_map& regexps);
bool matches(const regexps_map& regexps, const std::string& str,
	     bool * by_exclusion = NULL);


struct account;
struct transaction
{
  account * acct;
  amount *  cost;

  std::string note;

  bool is_virtual;
  bool must_balance;
  bool specified;

  transaction(account * _acct = NULL, amount * _cost = NULL)
    : acct(_acct), cost(_cost),
      is_virtual(false), must_balance(true), specified(false) {}

  ~transaction() {
    if (cost)
      delete cost;
  }

  const std::string acct_as_str() const;

  void print(std::ostream& out, bool display_quantity = true,
	     bool display_price = true) const;
};


struct entry
{
  std::time_t date;
  std::string code;
  std::string desc;

  bool cleared;

  std::list<transaction *> xacts;

  entry() : cleared(false) {}

  // If we're running as a command-line tool, it's cheaper to just
  // throw away the heap on exit, than spend time freeing things up
  // like a good citizen.

  ~entry() {
    for (std::list<transaction *>::iterator i = xacts.begin();
	 i != xacts.end();
	 i++) {
      delete *i;
    }
  }

  bool matches(const std::list<mask>& regexps) const;
  bool validate(bool show_unaccounted = false) const;

  void print(std::ostream& out, bool shortcut = true) const;
};

struct cmp_entry_date {
  bool operator()(const entry * left, const entry * right) {
    return std::difftime(left->date, right->date) < 0;
  }
};

typedef std::vector<entry *>         entries_list;
typedef entries_list::iterator       entries_list_iterator;
typedef entries_list::const_iterator entries_list_const_iterator;


struct totals
{
  typedef std::map<const std::string, amount *>  map;
  typedef map::iterator                          iterator;
  typedef map::const_iterator                    const_iterator;
  typedef std::pair<const std::string, amount *> pair;

  map amounts;

  ~totals();

  void credit(const amount * val) {
    std::pair<iterator, bool> result =
      amounts.insert(pair(val->comm_symbol(), val->copy()));
    if (! result.second)
      amounts[val->comm_symbol()]->credit(val);
  }
  void credit(const totals& other);

  bool is_zero() const;

  void print(std::ostream& out, int width) const;

  // Returns an allocated entity
  amount * sum(const std::string& comm) {
    return amounts[comm];
  }
};


typedef std::map<const std::string, account *>  accounts_map;
typedef accounts_map::iterator                  accounts_map_iterator;
typedef std::pair<const std::string, account *> accounts_map_pair;

struct account
{
  account * parent;

  std::string name;
#ifdef READ_GNUCASH
  commodity * comm;             // default commodity for this account
#endif
  totals      balance;          // optional, parse-time computed balance
  int         checked;        // 'balance' uses this for speed's sake
  accounts_map  children;

  mutable std::string full_name;

  account() : parent(NULL), checked(0) {}

  account(const std::string& _name, struct account * _parent = NULL)
    : parent(_parent), name(_name), checked(0) {}

  const std::string as_str() const {
    if (! parent)
      return name;
    else if (full_name.empty())
      full_name = parent->as_str() + ":" + name;

    return full_name;
  }
};


struct state
{
  commodities_map commodities;
  accounts_map    accounts;
  accounts_map    accounts_cache; // maps full names to accounts
  entries_list    entries;
  totals          prices;
  int             current_year;

  typedef std::map<std::list<mask> *,
		   std::list<transaction *> *> virtual_map;

  typedef std::pair<std::list<mask> *,
		    std::list<transaction *> *> virtual_map_pair;

  typedef virtual_map::const_iterator virtual_map_iterator;

  bool        compute_balances;
  virtual_map virtual_mapping;

  ~state();

  void record_price(const std::string& setting);

  template<typename Compare>
  void sort(Compare comp) {
    std::sort(entries.begin(), entries.end(), comp);
  }
  void print(std::ostream& out, regexps_map& regexps, bool shortcut) const;

  account * find_account(const std::string& name, bool create = true);
};

extern state * main_ledger;
extern bool    use_warnings;

inline commodity::commodity(const std::string& sym, bool pre, bool sep,
			    bool thou, bool euro, int prec)
  : symbol(sym), prefix(pre), separate(sep),
    thousands(thou), european(euro), precision(prec) {
  std::pair<commodities_map_iterator, bool> result =
    main_ledger->commodities.insert(commodities_map_pair(sym, this));
  assert(result.second);
}

} // namespace ledger

#endif // _LEDGER_H
