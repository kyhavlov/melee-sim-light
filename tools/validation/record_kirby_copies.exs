Logger.configure(level: :warning)
alias ExPhil.Bridge.MeleePort

# Records a scripted Kirby (port 1) against a scripted opponent (port 2) so
# each copy ability is swallowed, fired on the ground and in the air, dropped
# by a taunt, re-swallowed, then lost by hits and by death. Run from an
# ExPhil checkout with its compiled bridge on the Elixir code path:
#
#   devenv shell -- elixir -pa '_build/dev/lib/*/ebin' \
#     tools/validation/record_kirby_copies.exs mario /out/dir dolphin-emu-headless melee.iso
#
# "moves" records Kirby's own specials (Final Cutter, Hammer, Stone) around
# Battlefield's platforms and ledges against a passive Fox.
[opponent, directory, dolphin, iso | rest] = System.argv()
stage_arg = case rest do [s | _] -> String.to_atom(s); [] -> nil end

opponents = ~w(mario luigi doc link ylink samus sheik ness pikachu pichu jigglypuff mewtwo dk yoshi roy ganondorf fox kirby moves)
unless opponent in opponents, do: raise("Unknown opponent: #{opponent}")
moves? = opponent == "moves"
dummy = if moves?, do: :fox, else: String.to_atom(opponent)
stage = stage_arg || if(moves?, do: :battlefield, else: :final_destination)

if Path.wildcard(Path.join(directory, "*.slp")) != [],
  do: raise("Output directory already contains recordings")

File.mkdir_p!(directory)
{:ok, bridge} = MeleePort.start_link([])

config = %{
  dolphin_path: Path.expand(dolphin),
  iso_path: Path.expand(iso),
  character: :kirby,
  dummy_character: dummy,
  stage: stage,
  controller_port: 1,
  opponent_port: 2,
  dummy_mode: "external",
  dummy_cpu_level: 0,
  online_delay: 0,
  headless: true,
  no_audio: true,
  emulation_speed: 0.0,
  accurate_nmsub: true,
  slippi_port: 51990 + Enum.find_index(opponents, &(&1 == opponent)),
  replay_dir: directory
}

# Kirby action ids (ftKb_MS_*): 353..357 inhale, 358..366 holding a victim,
# 367/368 swallow, 369/370 spit, 385..392 Final Cutter, 393..398 Stone.
eating? = fn a -> a in 358..366 end
busy? = fn a -> a >= 341 end

try do
  {:ok, _} = MeleePort.init_console(bridge, config, 120_000)
  neutral = %{main_stick: %{x: 0.5, y: 0.5}, c_stick: %{x: 0.5, y: 0.5}, buttons: %{}}
  input = fn x, y, buttons -> %{neutral | main_stick: %{x: x, y: y}, buttons: buttons} end

  # phase, phase start frame, cycle count
  init = %{phase: :approach, since: 0, cycle: 0, last: nil, fails: 0}

  result =
    Enum.reduce_while(1..20000, init, fn _, st ->
      case MeleePort.step(bridge, [poll: true], 30_000) do
        {:ok, gs} ->
          f = gs.frame
          me = gs.players[1]
          them = gs.players[2]
          dx = them.x - me.x
          toward = if dx > 0, do: 1.0, else: 0.0
          away = 1.0 - toward
          t = f - st.since

          if rem(f, 300) == 0,
            do:
              IO.inspect(
                {f, st.phase, st.cycle, Map.take(me, [:x, :y, :action, :percent, :stock]),
                 Map.take(them, [:x, :y, :action, :percent, :stock])}
              )

          # Phase transitions (copy scenarios).
          st =
            cond do
              moves? -> st
              st.phase == :approach and abs(dx) <= 13 and me.on_ground -> %{st | phase: :inhale, since: f}
              st.phase == :inhale and me.action >= 399 -> %{st | phase: :taunt, since: f}   # still wearing a hat: B fired a copy
              st.phase == :inhale and eating?.(me.action) -> %{st | phase: :swallow, since: f}
              st.phase == :inhale and me.action in 367..368 -> %{st | phase: :use, since: f}
              st.phase == :inhale and t > 300 and st.fails >= 6 -> %{st | phase: :walkoff, since: f}
              st.phase == :inhale and t > 300 -> %{st | phase: :approach, since: f, fails: st.fails + 1}
              st.phase == :swallow and me.action in 367..368 -> %{st | phase: :use, since: f}
              st.phase == :swallow and t > 12 and not eating?.(me.action) and not busy?.(me.action) -> %{st | phase: :use, since: f}
              st.phase == :swallow and t > 240 -> %{st | phase: :approach, since: f}
              st.phase == :use and t > 900 and st.cycle == 0 -> %{st | phase: :taunt, since: f}
              st.phase == :use and t > 900 -> %{st | phase: :hits, since: f}
              st.phase == :taunt and t > 400 -> %{st | phase: :approach, since: f, cycle: st.cycle + 1}
              st.phase == :hits and t > 500 -> %{st | phase: :walkoff, since: f}
              true -> st
            end

          # Kirby (port 1).
          p1 =
            cond do
              moves? ->
                # Battlefield: platforms at |x| 20..57, y 27; ledges at |x| 68.
                cond do
                  t < 60 -> neutral
                  rem(t, 600) in 0..30 -> input.(0.0, 0.5, %{})            # walk left toward the platform
                  rem(t, 600) in 31..34 or rem(t, 600) in 50..53 -> input.(0.5, 0.5, %{x: true})  # double jump
                  rem(t, 600) in 70..72 -> input.(0.5, 0.0, %{b: true})     # aerial Stone
                  rem(t, 600) in 73..140 -> input.(0.5, 0.0, %{})           # stay stone
                  rem(t, 600) in 160..200 -> input.(0.0, 0.5, %{})          # walk toward the ledge
                  rem(t, 600) in 201..204 -> input.(0.0, 0.5, %{b: true})   # Hammer near the ledge
                  rem(t, 600) in 260..263 -> input.(0.5, 1.0, %{b: true})   # Final Cutter
                  rem(t, 600) in 330..333 -> input.(1.0, 0.5, %{b: true})   # Hammer toward center
                  rem(t, 600) in 380..410 -> input.(1.0, 0.5, %{})          # dash right
                  rem(t, 600) in 411..413 -> input.(1.0, 0.5, %{a: true})   # dash attack
                  rem(t, 600) in 440..443 -> input.(0.5, 0.5, %{x: true})
                  rem(t, 600) in 452..455 -> input.(0.5, 0.5, %{b: true})   # aerial Hammer
                  rem(t, 600) in 500..503 -> input.(0.5, 0.0, %{b: true})   # grounded Stone
                  rem(t, 600) in 504..560 -> input.(0.5, 0.0, %{})
                  t > 4200 -> input.(0.0, 0.5, %{})                          # walk off until stocks are gone
                  true -> neutral
                end
              st.phase == :approach and abs(dx) > 13 -> input.(toward, 0.5, %{})
              st.phase == :approach and (dx > 0) != (me.facing > 0) -> input.(toward, 0.5, %{})   # face the opponent
              st.phase == :approach -> neutral
              st.phase == :inhale and (dx > 0) != (me.facing > 0) and rem(t, 100) >= 90 -> input.(toward, 0.5, %{})   # turn to face
              st.phase == :inhale and rem(t, 100) < 90 -> input.(0.5, 0.5, %{b: true})   # press, hold, release
              st.phase == :inhale -> neutral
              st.phase == :swallow and t < 8 -> input.(0.5, 0.0, %{})
              st.phase == :swallow and rem(t, 20) < 4 -> input.(0.5, 0.0, %{})
              st.phase == :swallow -> neutral
              st.phase == :use ->
                cond do
                  rem(t, 300) in 20..90 -> input.(0.5, 0.5, %{b: true})     # held neutral B (charges), released at 91
                  rem(t, 300) in 130..133 -> input.(0.5, 0.5, %{b: true})   # tapped neutral B
                  rem(t, 300) in 160..163 -> input.(0.5, 0.5, %{b: true})   # second tap
                  rem(t, 300) in 190..193 -> input.(0.5, 0.5, %{x: true})
                  rem(t, 300) in 200..240 -> input.(0.5, 0.5, %{b: true})   # aerial neutral B, released in the air
                  rem(t, 300) in 270..273 and abs(dx) > 40 -> input.(toward, 0.5, %{})
                  true -> neutral
                end
              st.phase == :taunt and me.action == 14 and rem(t, 40) < 6 -> input.(0.5, 0.5, %{d_up: true})   # taunt from Wait
              st.phase == :taunt -> neutral
              st.phase == :hits -> neutral
              st.phase == :walkoff -> input.(0.0, 0.5, %{})
              true -> neutral
            end

          # Opponent (port 2).
          p2 =
            cond do
              moves? -> neutral
              st.phase in [:approach, :inhale, :swallow] and abs(dx) > 20 and t < 200 -> input.(away, 0.5, %{})  # step in from the spawn
              st.phase in [:approach, :inhale, :swallow] -> neutral
              st.phase == :use and opponent in ["yoshi", "kirby"] and rem(t, 300) in 10..60 and abs(dx) > 8 -> input.(away, 0.5, %{})  # contact copies
              st.phase == :use and opponent == "kirby" and rem(t, 300) in 250..280 -> input.(0.5, 0.5, %{b: true})  # the other Kirby inhales
              st.phase == :use and opponent == "kirby" and rem(t, 300) in 285..288 -> input.(0.5, 0.5, %{a: true})  # and spits
              st.phase == :use and abs(dx) < 28 -> input.(1.0 - away, 0.5, %{})   # keep projectile distance
              st.phase == :use -> neutral
              st.phase == :taunt -> neutral
              st.phase == :hits and abs(dx) > 9 -> input.(away, 0.5, %{})
              st.phase == :hits and rem(t, 45) in 0..3 -> input.(0.5, 0.5, %{a: true})   # jab / tilt Kirby to shake the hat loose
              st.phase == :hits and rem(t, 45) in 20..23 -> input.(away, 0.5, %{a: true})
              true -> neutral
            end

          :ok = MeleePort.send_controller(bridge, p1)
          :ok = MeleePort.send_controller(bridge, Map.put(p2, :port, 2))
          {:cont, %{st | last: f}}

        {:postgame, _} -> {:halt, :complete}
        {:game_ended, _} -> {:halt, :complete}
        {:menu, _} when is_integer(st.last) and st.last >= 900 -> {:halt, :complete}
        {:menu, _} -> {:cont, st}
        :no_frame -> {:cont, st}
        other ->
          IO.inspect(other, label: "unexpected")
          {:halt, other}
      end
    end)

  unless result == :complete, do: raise("Recording did not finish: #{inspect(result)}")
  IO.puts("Completed Kirby copy recording #{opponent}: #{directory}")
after
  MeleePort.stop(bridge)
end
