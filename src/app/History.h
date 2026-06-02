#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace app {

/// <summary>A single reversible action: a label and the closures to undo / redo it.</summary>
struct Action {
    std::string label;
    std::function<void()> undo;
    std::function<void()> redo;
};

/// <summary>A bounded undo/redo stack of closure-based actions.</summary>
class History {
public:
    /// <summary>Records a freshly-performed action. Clears the redo stack.</summary>
    void push(Action a) {
        redo_.clear();
        undo_.push_back(std::move(a));
        if (undo_.size() > kMax) undo_.erase(undo_.begin());
    }

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

    /// <summary>Undoes the most recent action and moves it to the redo stack. Returns its label, or "".</summary>
    std::string undo() {
        if (undo_.empty()) return {};
        Action a = std::move(undo_.back());
        undo_.pop_back();
        a.undo();
        std::string label = a.label;
        redo_.push_back(std::move(a));
        return label;
    }

    /// <summary>Redoes the most recently undone action. Returns its label, or "".</summary>
    std::string redo() {
        if (redo_.empty()) return {};
        Action a = std::move(redo_.back());
        redo_.pop_back();
        a.redo();
        std::string label = a.label;
        undo_.push_back(std::move(a));
        return label;
    }

    void clear() { undo_.clear(); redo_.clear(); }

private:
    static constexpr std::size_t kMax = 64;
    std::vector<Action> undo_;
    std::vector<Action> redo_;
};

}
