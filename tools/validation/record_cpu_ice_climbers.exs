Logger.configure(level: :warning)
alias ExPhil.Bridge.MeleePort

# Records a human Fox (scripted controller on port 1) against a game-driven
# CPU Ice Climbers on port 2. Run from an ExPhil checkout with its compiled
# bridge on the Elixir code path.
[scenario, directory, dolphin, iso | rest] = System.argv()
level = case rest do [l | _] -> String.to_integer(l); [] -> 5 end

# "laser": Fox lasers and dashes on Final Destination, then walks off.
# "desync": Fox chases and attacks on Battlefield to split Popo from Nana,
# then walks off.
scenarios = ["laser", "desync"]
unless scenario in scenarios, do: raise("Unknown recording scenario: #{scenario}")

if Path.wildcard(Path.join(directory, "*.slp")) != [],
  do: raise("Output directory already contains recordings")

File.mkdir_p!(directory)
{:ok, bridge} = MeleePort.start_link([])

config = %{
  dolphin_path: Path.expand(dolphin),
  iso_path: Path.expand(iso),
  character: :fox,
  dummy_character: :popo,
  stage: if(scenario == "desync", do: :battlefield, else: :final_destination),
  controller_port: 1,
  opponent_port: 2,
  dummy_mode: "cpu",
  dummy_cpu_level: level,
  online_delay: 0,
  headless: true,
  no_audio: true,
  emulation_speed: 0.0,
  accurate_nmsub: true,
  slippi_port: 51980 + Enum.find_index(scenarios, &(&1 == scenario)),
  replay_dir: directory
}

try do
  {:ok, _} = MeleePort.init_console(bridge, config, 120_000)
  neutral = %{main_stick: %{x: 0.5, y: 0.5}, c_stick: %{x: 0.5, y: 0.5}, buttons: %{}}
  input = fn x, y, buttons -> %{neutral | main_stick: %{x: x, y: y}, buttons: buttons} end

  result =
    Enum.reduce_while(1..12000, nil, fn _, previous ->
      case MeleePort.step(bridge, [poll: true], 30_000) do
        {:ok, gs} ->
          f = gs.frame

          if rem(f, 300) == 0,
            do:
              IO.inspect(
                {f, Map.take(gs.players[1], [:x, :y, :action, :percent, :stock]),
                 Map.take(gs.players[2], [:x, :y, :action, :percent, :stock])}
              )

          me = gs.players[1]
          them = gs.players[2]
          dx = them.x - me.x
          toward = if dx > 0, do: 1.0, else: 0.0

          # laser: lasers, a few dashes and shields to draw the CPU in.
          # desync: chase the leader; up close alternate aerials, up-smash
          # and grabs so hits and throws separate the climbers. Both walk
          # off until the match ends.
          p1 =
            cond do
              scenario == "laser" and f >= 3600 -> input.(0.0, 0.5, %{})
              scenario == "desync" and f >= 6000 -> input.(0.0, 0.5, %{})
              scenario == "desync" and abs(dx) > 18 and rem(f, 2) == 0 -> input.(toward, 0.5, %{})
              scenario == "desync" and abs(dx) > 18 -> neutral
              scenario == "desync" and rem(f, 40) < 4 -> input.(0.5, 1.0, %{x: true})
              scenario == "desync" and rem(f, 40) in 6..9 -> input.(toward, 0.5, %{a: true})
              scenario == "desync" and rem(f, 40) in 16..18 -> input.(0.5, 0.5, %{z: true})
              scenario == "desync" and rem(f, 40) in 22..23 -> input.(0.5, 1.0, %{})
              scenario == "desync" and rem(f, 40) in 28..31 -> %{neutral | c_stick: %{x: 0.5, y: 1.0}}
              scenario == "desync" and rem(f, 40) in 34..36 -> input.(0.5, 0.0, %{b: true})
              scenario == "desync" -> neutral
              f >= 60 and rem(f, 90) == 0 -> input.(0.5, 0.5, %{b: true})
              f >= 60 and rem(f, 90) == 30 -> input.(1.0, 0.5, %{})
              f >= 60 and rem(f, 90) == 45 -> input.(0.0, 0.5, %{})
              f >= 60 and rem(f, 90) in 60..70 -> input.(0.5, 0.5, %{l: true})
              true -> neutral
            end

          :ok = MeleePort.send_controller(bridge, p1)
          {:cont, f}

        {:postgame, _} ->
          {:halt, :complete}

        {:game_ended, _} ->
          {:halt, :complete}

        {:menu, _} when is_integer(previous) and previous >= 900 ->
          {:halt, :complete}

        {:menu, _} ->
          {:cont, previous}

        :no_frame ->
          {:cont, previous}

        other ->
          IO.inspect(other, label: "unexpected")
          {:halt, other}
      end
    end)

  unless result == :complete, do: raise("Recording did not finish: #{inspect(result)}")
  IO.puts("Completed CPU Ice Climbers #{scenario} level #{level}: #{directory}")
after
  MeleePort.stop(bridge)
end
