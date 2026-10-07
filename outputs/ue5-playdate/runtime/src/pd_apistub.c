/* Installs no-op fallbacks for every documented Playdate Lua API that the
 * runtime does not implement natively. This converts hard crashes
 * ("field 'x' is not callable") into silently missing features.
 *
 * Existing implementations are never overwritten: a stub is only installed
 * when the slot is nil. Namespaces are created as needed.
 */
#include "pd_runtime.h"

#include <stdio.h>
#include <string.h>

#include "pd_apistubs.h"

static int pd_api_noop(lua_State *L) {
    (void)L;
    return 0;
}

/* Like pd_api_noop but logs the first call per stub (PD_STUB_LOG=1). */
static int pd_api_noop_logged(lua_State *L) {
    if (lua_toboolean(L, lua_upvalueindex(2)) == 0) {
        fprintf(stderr, "[stub called] %s\n", lua_tostring(L, lua_upvalueindex(1)));
        lua_pushboolean(L, 1);
        lua_replace(L, lua_upvalueindex(2));
    }
    return 0;
}

/* Walk a dotted/colon path from _G, creating intermediate tables.
 * Returns 1 with the parent container on the stack top and writes the
 * final key name into `leaf`; returns 0 if an intermediate exists but is
 * not indexable (never stomp on non-table values). */
static int resolve_parent(lua_State *L, const char *path, char *leaf, size_t leaf_sz) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);

    /* method separator ':' behaves like '.' for our flat class tables */
    for (char *p = buf; *p; p++)
        if (*p == ':') *p = '.';

    lua_pushglobaltable(L);
    char *save = NULL;
    char *tok = strtok_r(buf, ".", &save);
    while (tok) {
        char *next = strtok_r(NULL, ".", &save);
        if (!next) {
            snprintf(leaf, leaf_sz, "%s", tok);
            return 1; /* parent on stack */
        }
        lua_getfield(L, -1, tok);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, tok);
        } else if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) {
            lua_pop(L, 2);
            return 0;
        }
        lua_remove(L, -2);
        tok = next;
    }
    lua_pop(L, 1);
    return 0;
}

void pd_install_api_stubs(lua_State *L) {
    int installed = 0;
    for (int i = 0; pd_api_stub_paths[i]; i++) {
        char leaf[128];
        if (!resolve_parent(L, pd_api_stub_paths[i], leaf, sizeof(leaf)))
            continue;
        if (!lua_istable(L, -1)) { /* can't setfield on userdata parents */
            lua_pop(L, 1);
            continue;
        }
        lua_getfield(L, -1, leaf);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            if (getenv("PD_STUB_LOG")) {
                lua_pushstring(L, pd_api_stub_paths[i]);
                lua_pushboolean(L, 0);
                lua_pushcclosure(L, pd_api_noop_logged, 2);
            } else {
                lua_pushcfunction(L, pd_api_noop);
            }
            lua_setfield(L, -2, leaf);
            installed++;
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    if (getenv("PD_TRACE"))
        fprintf(stderr, "[stubs] installed %d API fallbacks\n", installed);

    /* Known pdex.bin C-extension libs some hybrid games require at load
       time. Stubbing them lets the Lua side run (minus the effect the
       extension provided). starlib: b360's starfield renderer. */
    luaL_dostring(L,
        "if starlib == nil then\n"
        "  local function stubnew(...)\n"
        "    return setmetatable({}, { __index = function()\n"
        "      return function(self) return self end\n"
        "    end })\n"
        "  end\n"
        "  starlib = { new = stubnew, starfield = { new = stubnew } }\n"
        "end\n"
        /* playdate.scoreboards: network-backed; report failure via the
           async callback like the SDK does when offline */
        "if playdate.scoreboards == nil then\n"
        "  local function offline(...)\n"
        "    for i = 1, select('#', ...) do\n"
        "      local cb = select(i, ...)\n"
        "      if type(cb) == 'function' then cb(nil, 'network unavailable') end\n"
        "    end\n"
        "  end\n"
        "  playdate.scoreboards = setmetatable({}, {\n"
        "    __index = function() return offline end })\n"
        "end\n");

    /* playdate.pathfinder: pure-Lua A* implementation (firmware provides
       this in C; the stub loop above only made no-ops, so replace it). */
    if (luaL_dostring(L,
        "do\n"
        "local Node = {}; Node.__index = Node\n"
        "local function newnode(g, id, x, y)\n"
        "  return setmetatable({id=id or 0, x=x or 0, y=y or 0, _g=g, _c={}}, Node)\n"
        "end\n"
        "function Node:addConnection(n, w, r)\n"
        "  self._c[n] = w or 1; if r then n._c[self] = w or 1 end\n"
        "end\n"
        "function Node:addConnections(ns, ws, r)\n"
        "  for i, n in ipairs(ns) do self:addConnection(n, ws and ws[i], r) end\n"
        "end\n"
        "function Node:addConnectionToNodeWithXY(x, y, w, r)\n"
        "  local n = self._g and self._g:nodeWithXY(x, y)\n"
        "  if n then self:addConnection(n, w, r) end\n"
        "end\n"
        "function Node:connectedNodes()\n"
        "  local t = {}; for n in pairs(self._c) do t[#t+1] = n end; return t\n"
        "end\n"
        "function Node:removeConnection(n, r)\n"
        "  self._c[n] = nil; if r then n._c[self] = nil end\n"
        "end\n"
        "function Node:removeAllConnections(incoming)\n"
        "  self._c = {}\n"
        "  if incoming and self._g then\n"
        "    for _, m in ipairs(self._g._nodes) do m._c[self] = nil end\n"
        "  end\n"
        "end\n"
        "function Node:setXY(x, y) self.x = x; self.y = y end\n"
        "local Graph = {}; Graph.__index = Graph\n"
        "local graph = {}\n"
        "function graph.new(count, coords)\n"
        "  local g = setmetatable({_nodes={}}, Graph)\n"
        "  if count then\n"
        "    for i = 1, count do\n"
        "      local x, y\n"
        "      if coords and coords[i] then x, y = coords[i][1], coords[i][2] end\n"
        "      g._nodes[#g._nodes+1] = newnode(g, i, x, y)\n"
        "    end\n"
        "  end\n"
        "  return g\n"
        "end\n"
        "function graph.new2DGrid(w, h, diag, included)\n"
        "  local g = graph.new(); g._w, g._h = w, h\n"
        "  local nodes, id = {}, 0\n"
        "  for row = 1, h do for col = 1, w do\n"
        "    id = id + 1\n"
        "    if (not included) or included[id] == 1 then\n"
        "      local n = newnode(g, id, col, row)\n"
        "      nodes[id] = n; g._nodes[#g._nodes+1] = n\n"
        "    end\n"
        "  end end\n"
        "  for _, n in pairs(nodes) do\n"
        "    local col, row = n.x, n.y\n"
        "    local function link(dc, dr, wt)\n"
        "      local c2, r2 = col+dc, row+dr\n"
        "      if c2 >= 1 and c2 <= w and r2 >= 1 and r2 <= h then\n"
        "        local m = nodes[(r2-1)*w + c2]\n"
        "        if m then n._c[m] = wt end\n"
        "      end\n"
        "    end\n"
        "    link(1,0,10); link(-1,0,10); link(0,1,10); link(0,-1,10)\n"
        "    if diag then link(1,1,14); link(1,-1,14); link(-1,1,14); link(-1,-1,14) end\n"
        "  end\n"
        "  return g\n"
        "end\n"
        "function Graph:addNewNode(id, x, y, conn, weights, r)\n"
        "  local n = newnode(self, id, x, y)\n"
        "  self._nodes[#self._nodes+1] = n\n"
        "  if conn then\n"
        "    for i, cid in ipairs(conn) do\n"
        "      local m = self:nodeWithID(cid)\n"
        "      if m then n:addConnection(m, weights and weights[i], r) end\n"
        "    end\n"
        "  end\n"
        "  return n\n"
        "end\n"
        "function Graph:addNewNodes(count)\n"
        "  local out, maxid = {}, 0\n"
        "  for _, n in ipairs(self._nodes) do if n.id > maxid then maxid = n.id end end\n"
        "  for i = 1, count do out[#out+1] = self:addNewNode(maxid + i) end\n"
        "  return out\n"
        "end\n"
        "function Graph:addNode(n) n._g = self; self._nodes[#self._nodes+1] = n end\n"
        "function Graph:allNodes()\n"
        "  local t = {}; for i, n in ipairs(self._nodes) do t[i] = n end; return t\n"
        "end\n"
        "function Graph:nodeWithID(id)\n"
        "  for _, n in ipairs(self._nodes) do if n.id == id then return n end end\n"
        "end\n"
        "function Graph:nodeWithXY(x, y)\n"
        "  for _, n in ipairs(self._nodes) do if n.x == x and n.y == y then return n end end\n"
        "end\n"
        "function Graph:removeNode(rm)\n"
        "  for i, n in ipairs(self._nodes) do\n"
        "    if n == rm then table.remove(self._nodes, i) break end\n"
        "  end\n"
        "  for _, n in ipairs(self._nodes) do n._c[rm] = nil end\n"
        "end\n"
        "function Graph:removeNodeWithID(id)\n"
        "  local n = self:nodeWithID(id); if n then self:removeNode(n) end; return n\n"
        "end\n"
        "function Graph:removeNodeWithXY(x, y)\n"
        "  local n = self:nodeWithXY(x, y); if n then self:removeNode(n) end; return n\n"
        "end\n"
        "function Graph:addConnections(conns)\n"
        "  for id, list in pairs(conns) do\n"
        "    local n = self:nodeWithID(id)\n"
        "    if n then\n"
        "      for i = 1, #list, 2 do\n"
        "        local m = self:nodeWithID(list[i])\n"
        "        if m then n._c[m] = list[i+1] end\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "end\n"
        "function Graph:addConnectionToNodeWithID(a, b, w, r)\n"
        "  local n, m = self:nodeWithID(a), self:nodeWithID(b)\n"
        "  if n and m then n:addConnection(m, w, r) end\n"
        "end\n"
        "function Graph:removeAllConnections()\n"
        "  for _, n in ipairs(self._nodes) do n._c = {} end\n"
        "end\n"
        "function Graph:removeAllConnectionsFromNodeWithID(id, incoming)\n"
        "  local n = self:nodeWithID(id); if n then n:removeAllConnections(incoming) end\n"
        "end\n"
        "function Graph:setXYForNodeWithID(id, x, y)\n"
        "  local n = self:nodeWithID(id); if n then n.x, n.y = x, y end\n"
        "end\n"
        "function Graph:findPath(s, goal, heur, adjacent)\n"
        "  if not (s and goal) then return nil end\n"
        "  heur = heur or function(a, b)\n"
        "    return math.abs(a.x - b.x) + math.abs(a.y - b.y)\n"
        "  end\n"
        "  local open = {[s] = true}\n"
        "  local came, gsc, fsc = {}, {[s] = 0}, {[s] = heur(s, goal)}\n"
        "  while next(open) do\n"
        "    local cur, best\n"
        "    for n in pairs(open) do\n"
        "      local f = fsc[n] or math.huge\n"
        "      if not cur or f < best then cur, best = n, f end\n"
        "    end\n"
        "    if cur == goal or (adjacent and cur._c[goal]) then\n"
        "      local path = {cur}\n"
        "      while came[cur] do cur = came[cur]; table.insert(path, 1, cur) end\n"
        "      return path\n"
        "    end\n"
        "    open[cur] = nil\n"
        "    for m, w in pairs(cur._c) do\n"
        "      local t = gsc[cur] + (w or 1)\n"
        "      if t < (gsc[m] or math.huge) then\n"
        "        came[m] = cur; gsc[m] = t; fsc[m] = t + heur(m, goal)\n"
        "        open[m] = true\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  return nil\n"
        "end\n"
        "function Graph:findPathWithIDs(a, b, ...)\n"
        "  local s, g2 = self:nodeWithID(a), self:nodeWithID(b)\n"
        "  local p = self:findPath(s, g2, ...)\n"
        "  if not p then return nil end\n"
        "  local ids = {}\n"
        "  for i, n in ipairs(p) do ids[i] = n.id end\n"
        "  return ids\n"
        "end\n"
        "playdate.pathfinder = {\n"
        "  graph = graph,\n"
        "  node = { new = function(id, x, y) return newnode(nil, id, x, y) end },\n"
        "}\n"
        "end\n") != LUA_OK) {
        fprintf(stderr, "[pathfinder] init failed: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}
