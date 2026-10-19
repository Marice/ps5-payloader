// PayLoader - Stable form row ids for things known by a text key (a slot, a pid, a URL).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A form row needs an int id; a payload is known by its slot. The same key
// keeps the same id for the screen's life, so the focus stays on it when the
// list is built again.
//
//   RowIds ids;
//   form.add_action(ids.id(payload.slot), payload.title());
//   if (const std::string *slot = ids.slot(form.focus_id())) start(*slot);

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hui::concepts::payloader
{

class RowIds
{
  public:
    explicit RowIds(int first = 1000) : next_(first)
    {
    }

    int id(const std::string &key)
    {
        const auto found = ids_.find(key);
        if (found != ids_.end())
            return found->second;
        const int id = next_++;
        ids_.emplace(key, id);
        keys_.emplace_back(id, key);
        return id;
    }

    // The key of an id this map gave out, or null.
    const std::string *slot(int id) const
    {
        for (const auto &[known, key] : keys_)
            if (known == id)
                return &key;
        return nullptr;
    }

    const std::vector<std::pair<int, std::string>> &entries() const
    {
        return keys_;
    }

  private:
    std::map<std::string, int> ids_;
    std::vector<std::pair<int, std::string>> keys_;
    int next_;
};

} // namespace hui::concepts::payloader
