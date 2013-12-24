-module(id3as_media_c_app).

-behaviour(application).

%% Application callbacks
-export([start/2, stop/1]).

%% ===================================================================
%% Application callbacks
%% ===================================================================

start(_StartType, _StartArgs) ->
    id3as_media_c_sup:start_link().

stop(_State) ->
    ok.
