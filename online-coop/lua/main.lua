-- Seamless Co-op: stage 1, loopback session transport.
--
-- Attaches Co-op to the game's own Online screens (recomp host.native_ui hooks):
--   The connect entry of the Online menu is selectable, then the squad screen opens as in the stock game.
--   The create-a-private-squad option -> Host   (listen on 127.0.0.1:<port>)
--   The play-in-a-private-squad option -> Join   (connect to 127.0.0.1:<port>; the port is the passcode typed on the game's own keyboard)
-- The game raises its own confirmation dialog first; after Confirm the screen shows the game's own waiting window
-- and waits for the answer this script gives with host.native_ui.report('ok' | 'failed'). On 'ok' the game opens its own
-- private squad details screen (passcode, start button, leave button); the leave button runs the 'leave' action and the start button the 'start'
-- action. Host or Join while a session is already up shows that screen again (the game's own "already in a squad" state). On 'failed' the game
-- shows its own failure window.
-- The quick assignment and play-with-friends options are not mapped; the game answers them with its own "not available" message.
--
-- The connection work is done by the native provider shipped beside this file (provider.ini, native/). This script
-- only submits typed commands and shows the copied status. It never opens a socket. Nothing is synchronized between
-- the players yet: a successful session is a verified handshake only. Disabled or not installed, the game behaves
-- exactly as stock; with the provider missing or rejected, the Mods page says so and Host/Join do nothing.
local SERVICE='xcx.coop.loopback.v1'
local available=false
-- What this script last asked for, and the role of the session that is up ('host' or 'join'). The passcode the Details screen
-- shows is the loopback port: the code of the session that is up, and the one of the request in flight.
local attempt,role=nil,nil
local session_code,attempt_code,session_players=nil,nil,nil
-- The details screen's timer slot (its label is the game's own server rule and means nothing here) shows the players:
-- stage 1 has no real player list, so a host is alone ('1 / 32') and a loopback join is two ('2 / 32'). The count is out of 32,
-- the size of a squad in the game; the four combat party slots are a different thing (see docs/research/12).
local PLAYERS_LABEL='Players:'
local SQUAD_MAX=32
local function players_text(count) return count..' / '..SQUAD_MAX end

local function idle(message)
 return string.sub(message,1,12)=='Idle/offline' or string.sub(message,1,6)=='Failed'
end
local function connected(message) return string.sub(message,1,19)=='Transport connected' end

-- Party invite (stage 1 plumbing). The provider's status text ends with "; party=<state>" while the transport is connected;
-- the states are none, invite-sent, invite-received, in-party and invite-declined. The sandbox has no string.find, so the
-- field is read with string.sub. The page has one Party button (the page allows four actions): it sends an invite, or
-- accepts one that was received. Declining is in the provider and the tests but has no button yet.
local party_state,party_request,last_logged='none',nil,nil
local function party_of(message)
 local key='; party='
 for i=1,#message-#key+1 do
  if string.sub(message,i,i+#key-1)==key then return string.sub(message,i+#key) end
 end
 return nil
end
local function party_text(state)
 if state=='invite-received' then return 'Party invite received. Press Party invite to accept it.' end
 if state=='invite-sent' then return 'Party invite sent; waiting for an answer.' end
 if state=='in-party' then return 'You are in a party.' end
 if state=='invite-declined' then return 'Your party invite was declined. Press Party invite to try again.' end
 return 'No party invite.'
end

-- Host and Join stay enabled whenever the provider is healthy so either native option always responds; the provider
-- itself refuses a second session and says so. Leave is offered while a session or attempt exists.
local function refresh()
 local s=host.provider.status()
 available=s.available and s.service==SERVICE
 host.ui.enable('host',available)
 host.ui.enable('join',available)
 host.ui.enable('leave',available and not idle(s.message))
 host.ui.enable('party',available and connected(s.message))
 if not available then host.ui.status('Co-op transport unavailable: '..tostring(s.message)) end
end

return {
 init=function()
  host.settings.register {
   version=1,id='session',title='Seamless Co-op (loopback)',
   fields={{id='port',label='Loopback port',type='integer',min=1024,max=65535,default=27015}},
   actions={
    {id='host',label='Host',provider=true},
    {id='join',label='Join',provider=true},
    {id='leave',label='Leave',provider=true},
    {id='party',label='Party invite',provider=true}
   }
  }
  -- The start button has no action: it only leaves the menu on this machine, as in the game (owner decision, 2026-11-13).
  local ok,reason=host.native_ui.register {
   version=1,
   online_entry=true,
   squad_select={create_private='host',play_private='join'},
   details={leave='leave'}
  }
  if ok then host.log('coop: native Online screens registered')
  else host.log('coop: native Online screens unavailable: '..tostring(reason)) end
  host.ui.status(ok and 'Use the Online menu: the connect entry, then the create or play-in-a-private-squad option.'
                     or 'Native Online screens are not available in this build; use the buttons here.')
  refresh()
 end,
 events={
  ['host.started']=function() refresh(); host.log('coop: started') end,
  ['settings.changed']=function() host.ui.status('The port applies to the next Host or Join.') end,
  ['ui.action']=function(e)
   refresh()
   if e.action=='party' then
    -- One button: send an invite, or accept the one that was received.
    local code,text
    if not available then host.ui.status('Co-op transport unavailable.'); return end
    if party_state=='invite-received' then code,text=3,'Accepting the party invite.'
    elseif party_state=='none' or party_state=='invite-declined' then code,text=2,'Party invite sent.'
    else host.ui.status(party_state=='in-party' and 'You are already in a party.' or 'Waiting for an answer to your invite.'); return end
    local request,reason=host.provider.submit('probe',code)
    host.log('coop: party '..(code==2 and 'invite' or 'accept')..(request and ' accepted' or (' not accepted: '..tostring(reason))))
    if request then party_request=request end
    host.ui.status(request and text or ('Not accepted: '..tostring(reason)))
    return
   end
   if not available then host.native_ui.report('failed'); return end
   local request,reason
   if e.action=='leave' then request,reason=host.provider.submit('leave')
   elseif e.action=='host' or e.action=='join' then
    local port=host.settings.get('port')
    if e.action=='join' and e.text then
     -- The play-in-a-private-squad option: the passcode typed on the game's own keyboard. In stage 1 the passcode is the loopback port that
     -- the host's Details screen shows; a text that is not a usable port is a squad that cannot be found.
     local typed=tonumber(e.text)
     if not typed or typed%1~=0 or typed<1024 or typed>65535 then
      host.log('coop: join: the passcode '..e.text..' is not a loopback port')
      host.native_ui.report('failed')
      return
     end
     port=typed
    end
    attempt_code=e.action=='join' and e.text or tostring(port)
    request,reason=host.provider.submit(e.action,port)
   else return end
   host.log('coop: '..e.action..(request and ' accepted' or (' not accepted: '..tostring(reason))))
   if request then attempt=e.action end
   if not request and e.action~='leave' then host.native_ui.report('failed') end
   host.ui.status(request and 'Request accepted; waiting for the transport.' or ('Not accepted: '..tostring(reason)))
  end,
  ['provider.event']=function(e)
   if e.kind=='result' and party_request and e.request==party_request then
    -- the answer to a party command: not a session result, so it must not run the session handling below
    party_request=nil
    host.log('coop: party result '..e.result..': '..e.message)
    if e.result~='ok' then host.ui.status('Party: '..e.message) end
    refresh()
    return
   end
   if e.kind=='result' then
    host.ui.status(e.result..': '..e.message); host.log('coop: result '..e.result..': '..e.message)
    -- A request refused because a session is already up (Host or Join while one runs) shows the Details screen of that session
    -- (the game's own "already in a squad" state), where the leave button ends it; without an answer the screen would wait for the 20 second timeout.
    if e.result=='ok' then
     role=(attempt=='host' or attempt=='join') and attempt or nil
     if role then
      session_code=attempt_code
      session_players=players_text(role=='host' and 1 or 2)
      host.native_ui.set_session(true) -- the play-in-a-private-squad option no longer asks for a passcode while this session is up
      host.native_ui.report('ok',{passcode=session_code,mode=role=='host' and 'created' or 'joined',label=PLAYERS_LABEL,status=session_players})
     elseif attempt=='leave' then
      -- The leave button on the details screen: the session is over; the game's own toast says so once the squad screen is back.
      session_code=nil;session_players=nil
      host.native_ui.set_session(false)
      if host.native_ui.toast then host.native_ui.toast('You left the squad.') end
     end
    elseif e.result=='rejected' then
     host.native_ui.report('ok',{passcode=session_code or attempt_code,mode='in_squad',label=PLAYERS_LABEL,status=session_players or players_text(1)})
    elseif e.result=='failed' then
     role=nil
     host.native_ui.report('failed') -- the game's own failure window (create failed, or the squad was not found)
    end
   else
    local ps=party_of(e.message) or 'none'
    -- The host limits how many lines a mod may log, and the echo counter in this text changes every 250 ms, so the line is
    -- logged only when the connection state (the text before " | ") or the party state changes.
    local state=e.message
    for i=1,#e.message-2 do if string.sub(e.message,i,i+2)==' | ' then state=string.sub(e.message,1,i-1); break end end
    if state..'/'..ps~=last_logged then last_logged=state..'/'..ps; host.log('coop: transport '..e.message) end
    if ps~=party_state then
     local before=party_state
     party_state=ps
     host.log('coop: party '..ps)
     if ps~='none' or before~='none' then host.ui.status(party_text(ps)) end
    end
    if idle(e.message) then
     -- the session ended (left, failed or dropped); a Details screen still open for a joiner says so
     if role=='join' then host.native_ui.details_status('Closed') end
     role=nil;session_code=nil;session_players=nil
     host.native_ui.set_session(false)
    elseif role=='host' then
     -- the transport says whether a player is connected: the host's Players slot follows it (stage 1 has no real player list)
     local players=players_text(string.sub(e.message,1,19)=='Transport connected' and 2 or 1)
     if players~=session_players then
      session_players=players
      host.native_ui.details_status(players)
      host.log('coop: players '..players)
     end
    end
   end
   refresh()
  end
 },
 shutdown=function() host.log('coop: stopped') end
}
