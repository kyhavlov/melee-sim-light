Logger.configure(level: :warning)
alias ExPhil.Bridge.MeleePort

# Run from an ExPhil checkout with its compiled bridge on the Elixir code path.
[kind, directory, dolphin, iso] = System.argv()

kinds = ["bucket", "bucket_control", "bucket_missile", "judge_chef"]

unless kind in kinds, do: raise("Unknown recording scenario: #{kind}")

if Path.wildcard(Path.join(directory, "*.slp")) != [],
  do: raise("Output directory already contains recordings")

File.mkdir_p!(directory)
{:ok, bridge} = MeleePort.start_link([])

config = %{
  dolphin_path: Path.expand(dolphin),
  iso_path: Path.expand(iso),
  character: :gameandwatch,
  dummy_character: if(kind == "bucket_missile", do: :samus, else: :falco),
  stage: :final_destination,
  controller_port: 1,
  opponent_port: 2,
  dummy_mode: "external",
  dummy_cpu_level: 0,
  online_delay: 0,
  headless: true,
  no_audio: true,
  emulation_speed: 0.0,
  accurate_nmsub: true,
  slippi_port: 51970 + Enum.find_index(kinds, &(&1 == kind)),
  replay_dir: directory
}

try do
  {:ok, _} = MeleePort.init_console(bridge, config, 120_000)
  neutral = %{main_stick: %{x: 0.5, y: 0.5}, c_stick: %{x: 0.5, y: 0.5}, buttons: %{}}
  input = fn x, y, buttons -> %{neutral | main_stick: %{x: x, y: y}, buttons: buttons} end

  result =
    Enum.reduce_while(1..5000, nil, fn _, previous ->
      case MeleePort.step(bridge, [poll: true], 30_000) do
        {:ok, gs} ->
          f = gs.frame

          if rem(f, 120) == 0,
            do:
              IO.inspect(
                {f, Map.take(gs.players[1], [:x, :y, :action, :action_frame, :percent, :facing]),
                 Map.take(gs.players[2], [:x, :y, :action, :action_frame, :percent, :facing])}
              )

          finish = 1800
          p1 =
            cond do
              f >= finish -> input.(0.0, 0.5, %{})
              kind == "bucket_control" -> neutral
              kind == "judge_chef" and f >= 120 and f < 1100 and rem(f - 120, 65) == 0 ->
                input.(1.0, 0.5, %{b: true})
              kind == "judge_chef" and f >= 1200 -> input.(0.5, 0.5, %{b: true})
              kind == "judge_chef" -> neutral
              f >= 100 and f < 900 -> input.(0.5, 0.0, %{b: true})
              f == 1000 -> input.(0.5, 0.0, %{b: true})
              true -> neutral
            end
          p2 =
            cond do
              f >= finish or kind == "judge_chef" -> neutral
              f in [180, 360, 540] and kind == "bucket_missile" ->
                input.(0.0, 0.5, %{b: true})
              f in [180, 360, 540] -> input.(0.5, 0.5, %{b: true})
              true -> neutral
            end

          :ok = MeleePort.send_controller(bridge, p1)
          :ok = MeleePort.send_controller(bridge, Map.put(p2, :port, 2))
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
  IO.puts("Completed #{kind}: #{directory}")
after
  MeleePort.stop(bridge)
end
