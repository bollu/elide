#pragma once
#include "datastructures/debouncer.h"
#include <stack>

// T is a memento, from which the prior state can be
// entirely reconstructed.
template <typename T>
struct Undoer : public T {
public:
    // invariant: once we are `inUndoRedo`, the top of the `undoStack`
    // is the current state.
    void doUndo()
    {
        if (undoStack.empty()) {
            return;
        }
        if (!this->inUndoRedo) {
            this->inUndoRedo = true;
            // we are entering into undo/redo. Save the state right before
            // we began undo/redo, so that the user can redo their way
            // back to the 'earliest' state.
            T cur = getCheckpoint();
            redoStack.push(cur);

            T prev = undoStack.top();
            this->setCheckpoint(prev); // apply to setup the invariant.
        } else {
            assert(this->inUndoRedo);
            if (this->undoStack.size() == 1) {
                return; // this is already the state.
            }
            assert(this->undoStack.size() >= 2);
            // once the user has started undoing, then can only stop by
            // creating an undo memento.
            T cur = undoStack.top();
            redoStack.push(cur); // push the checkpoint of our state.
            undoStack.pop(); // pop cur.
            T prev = undoStack.top();
            this->setCheckpoint(prev); // apply.
        }
    }

    void doRedo()
    {
        if (redoStack.empty()) {
            return;
        }
        assert(this->inUndoRedo);
        T val = redoStack.top();
        undoStack.push(val); // push into redos.

        redoStack.pop();
        this->setCheckpoint(val); // apply the invariant.
    }

    // save the current state for later undoing and redoing.
    void mkUndoMemento()
    {
        // abort being in undo/redo mode.
        this->inUndoRedo = false;
        redoStack = {}; // nuke redo stack.

        T cur = getCheckpoint();
        if (!undoStack.empty()) {
            T& top = undoStack.top();
            if (top == cur) {
                return;
            } // state has not changed, no point.
        }
        undoStack.push(cur);
    }

    // save the current state, and debounce the save by 1 second.
    void mkUndoMementoRecent()
    {
        // if we are in undo/redo, then we should not *automatically* cause us to
        // quit undoRedo by making a memento.
        if (this->inUndoRedo) {
            return;
        }
        if (!debouncer.shouldAct()) {
            return;
        }
        mkUndoMemento();
    }

    Undoer() { }
    virtual ~Undoer() { }

protected:
    virtual T getCheckpoint()
    {
        return *(T*)(this);
    };
    virtual void setCheckpoint(T state)
    {
        *(T*)(this) = state;
    };

private:
    bool inUndoRedo = false; // if we are performing undo/redo.
    std::stack<T> undoStack; // stack of undos.
    std::stack<T> redoStack; // stack of redos.
    // make it greater than `0.1` seconds so it is 2x the perceptible limit
    // for humans. So it is a pause, but not necessarily a long one.
    Debouncer debouncer = Debouncer(std::chrono::seconds(0), 
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(150)));
};

