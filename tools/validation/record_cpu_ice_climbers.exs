Logger.configure(level: :warning)
alias ExPhil.Bridge.MeleePort

# Records a human Fox (scripted controller on port 1) against a game-driven
# CPU Ice Climbers on port 2. Run from an ExPhil checkout with its compiled
# bridge on the Elixir code path.
[directory, dolphin, iso | rest] = System.argv()
level = case rest do [l | _] -> String.to_integer(l); [] -> 5 end

if Path.wildcard(Path.join(directory, "*.slp")) != [],
  do: raise("Output directory already contains recordings")

File.mkdir_p!(directory)
{:ok, bridge} = MeleePort.start_link([])

config = %{
  dolphin_path: Path.expand(dolphin),
  iso_path: Path.expand(iso),
  character: :fox,
  dummy_character: :popo,
  stage: :final_destination,
  controller_port: 1,
  opponent_port: 2,
  dummy_mode: "cpu",
  dummy_cpu_level: level,
  online_delay: 0,
  headless: true,
  no_audio: true,
  emulation_speed: 0.0,
  accurate_nmsub: true,
  slippi_port: 51980,
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

          # Lasers, a few dashes and shields to draw the CPU in, then walk off
          # until the match ends.
          p1 =
            cond do
              f >= 3600 -> input.(0.0, 0.5, %{})
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
  IO.puts("Completed CPU Ice Climbers level #{level}: #{directory}")
after
  MeleePort.stop(bridge)
end
