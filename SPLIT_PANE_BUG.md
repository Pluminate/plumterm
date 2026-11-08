# Split Pane Bug Documentation

## Problem Summary
When splitting a pane (Cmd+Shift+R for right, Cmd+Shift+D for bottom), one of two things happens:
1. The new pane shows a duplicate/copy of the first terminal's content
2. The entire layout collapses and shows a fresh new terminal, losing the previous pane

## Expected Behavior
- Split right: Original pane stays on left with its session intact, new pane appears on right with fresh terminal
- Split down: Original pane stays on top with its session intact, new pane appears on bottom with fresh terminal
- Both panes should be independent with separate PTY sessions

## Actual Behavior
- Splitting causes the terminal content to be duplicated or lost
- The layout structure appears to change correctly (can see the resize divider)
- But the terminal content is either copied or completely reset

## Architecture

### Current Implementation
1. **LayoutNode.jsx** - Renders layout structure (panes and splits)
   - For pane nodes: renders a `<div>` wrapper with `<TerminalPane>` inside
   - Key used: `terminal-${node.sessionId}` to keep component mounted across layout changes
   - For split nodes: renders `<PanelGroup>` with two child `<Panel>` elements

2. **TerminalPane.jsx** - React wrapper for xterm.js terminal
   - Props: `sessionId`, `cwd`, `isActive`
   - On mount: sends `terminal.start` IPC with `sessionId` to main process
   - On unmount: sends `terminal.close` IPC to kill the PTY process

3. **App.jsx** - State management
   - State: `tabs` (array of tabs), `activeTabIndex`
   - Each tab has: `id`, `layout` (tree structure), `activeId` (current pane)
   - Handlers: `handleSplit`, `handleClose`, `handleNewTab`, etc.

4. **layoutTree.js** - Tree utilities
   - `splitNode(node, targetId, direction)` - Replaces target pane with a split containing original + new pane
   - When creating new pane: generates unique sessionId via `generateSessionId()`
   - New pane structure: `{ type: 'pane', id: newPaneId, sessionId: newPaneId }`

5. **main.js** - Electron main process
   - Maintains `ptyProcesses` Map: `sessionId -> ptyProcess`
   - IPC handler `terminal.start`: spawns new PTY if sessionId doesn't exist
   - IPC handler `terminal.keystroke`: sends input to PTY
   - IPC handler `terminal.close`: kills PTY and removes from map

## What We've Tried

### Attempt 1: React.memo with deep equality check
- **Idea:** Prevent unnecessary re-renders of LayoutNode
- **Result:** Made reconciliation issues worse, didn't fix the core problem

### Attempt 2: Using node.id as TerminalPane key
- **Idea:** Stable key to keep components mounted
- **Result:** Unmounting still occurred during layout tree restructuring

### Attempt 3: Conditional rendering with conditional TerminalPane mounting
- **Idea:** Always render both pane and split structures with display:none for inactive
- **Result:** Still caused unmounting

### Attempt 4: Absolute positioning - render TerminalPane in App.jsx on top of layout structure
- **Idea:** Decouple TerminalPane from layout tree by rendering in a separate layer
- **Problem:** Complex positioning logic, ResizeObserver for calculating pane locations
- **Result:** All panes rendered at same location until positions calculated, causing overlap and interaction issues

### Attempt 5: Current - sessionId as key in LayoutNode
- **Idea:** Key={`terminal-${node.sessionId}`} to preserve component identity
- **Status:** Still experiencing the duplicate/fresh terminal issue on split

## Symptoms

### Console Output Example
```
[PTY] Started /bin/zsh for session pane-35f69715 in /Users/ahmed
[PTY] Started /bin/zsh for session pane-ec425b0b in /Users/ahmed  // New pane session created correctly
[PTY] Started /bin/zsh for session pane-cae72f6d in /Users/ahmed  // Another split created another session
```

The PTY sessions are being created correctly in main.js. The issue is in the React rendering layer.

### Visual Behavior
1. Type command in first pane: `echo "hello"`
2. Press Cmd+Shift+R to split right
3. Expected: Left pane shows "hello", right pane is fresh
4. Actual: Right pane shows "hello" (copy of left) OR entire view resets

## Possible Root Causes

### 1. React Key/Reconciliation Issue
The layout tree is being reconstructed during split, and React might be reusing TerminalPane components incorrectly despite the sessionId key.

### 2. IPC Race Condition
- Old: TerminalPane mount sends `terminal.start` → main.js creates PTY
- New: When layout changes, maybe the wrong sessionId is being passed to a TerminalPane?
- Or: The new TerminalPane isn't mounting at all (component reuse instead of creation)?

### 3. State Update Ordering
When `handleSplit` updates the layout tree and changes `activeId`, there might be a race condition:
- Layout updates
- TerminalPane components re-render with new props
- But sessionId tracking gets confused

### 4. PanelGroup/react-resizable-panels Issue
The `<PanelGroup>` and `<Panel>` components might be interfering with React's reconciliation when the tree structure changes.

## Investigation Points

1. **Check React DevTools Profiler**
   - Does TerminalPane component get unmounted during split?
   - Or does it stay mounted with changed props?
   - Track which sessionId is being passed to each TerminalPane

2. **Add logging to identify the issue**
   - Log in TerminalPane: which sessionId is being mounted/updated
   - Log in main.js: when `terminal.start` is called and with what sessionId
   - Log in App.jsx: track what sessionId values are in the layout tree after split

3. **Check if sessionIds are being generated correctly**
   - Verify `generateSessionId()` creates unique IDs
   - Verify each new pane gets its own unique sessionId in the tree

4. **Verify TerminalPane mount/update/unmount lifecycle**
   - Add console.log with sessionId in all useEffect hooks
   - Confirm which PTY processes are being created/destroyed

## Files to Investigate
- `/Users/ahmed/Work/Terminal/client/src/App.jsx` - State updates on split
- `/Users/ahmed/Work/Terminal/client/src/components/LayoutNode.jsx` - Component structure
- `/Users/ahmed/Work/Terminal/client/src/components/TerminalPane.jsx` - IPC communication
- `/Users/ahmed/Work/Terminal/client/src/utils/layoutTree.js` - Layout tree mutations
- `/Users/ahmed/Work/Terminal/main.js` - PTY process management

## Next Steps
The issue is likely in how React is reconciling the component tree when layout changes. The fix probably requires:
1. Ensuring each TerminalPane component maintains identity through layout changes
2. OR: Preventing TerminalPane from unmounting even when parent structure changes
3. OR: Properly initializing new TerminalPane with correct sessionId on first mount
