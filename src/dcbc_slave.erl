-module(dcbc_slave).

-export([start_link/0, start_solver/3]).

-record(state, {solvers = dict:new(), nfree}).

-behavoir(gen_server).
-export([init/1, handle_call/3, handle_info/2, terminate/2]).

start_link() ->
    gen_server:start_link(?MODULE, [], []).

init([]) ->
    dcbc_registry:register(self(), slave),
    NFree = case erlang:system_info(logical_processors) of
        unknown -> 1;
        Num -> Num
    end,
    {ok, #state{nfree = NFree}}.

start_solver(Pid, Name, Args) ->
    gen_server:call(Pid, {start_solver, Name, Args}).

handle_call({start_solver, _Name, _Args}, _From, #state{nfree = 0} = State) ->
    {reply, {error, no_free_slots}, State};
handle_call({start_solver, Name, Args}, {MasterPid, _Tag}, #state{nfree = NFree} = State) ->
    {ok, ChildPid} = supervisor:start_child(dcbc_slave_sup, [Name, MasterPid, Args]),
    monitor(process, ChildPid),
    {reply, {ok, ChildPid}, State#state{nfree = NFree - 1,
                                        solvers = dict:store(ChildPid, 1, State#state.solvers)}}.

handle_info({'DOWN', _Ref, process, Pid, _Reason}, State) ->
    NCpu = dict:fetch(Pid, State#state.solvers),
    {noreply, State#state{nfree = State#state.nfree + NCpu,
                         solvers = dict:erase(Pid, State#state.solvers)}}.

terminate(Reason, State) ->
    ok.
