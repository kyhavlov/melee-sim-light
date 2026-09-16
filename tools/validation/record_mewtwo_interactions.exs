Logger.configure(level: :warning)
alias ExPhil.Bridge.MeleePort

# Run from an ExPhil checkout with its compiled bridge on the Elixir code path.
[kind, directory, dolphin, iso] = System.argv()

kinds = [
  "reflect",
  "reflect_control",
  "absorb",
  "absorb_control",
  "missile",
  "missile_control",
  "missile_early"
]

unless kind in kinds, do: raise("Unknown recording scenario: #{kind}")

if Path.wildcard(Path.join(directory, "*.slp")) != [],
  do: raise("Output directory already contains recordings")

File.mkdir_p!(directory)
{:ok, bridge} = MeleePort.start_link([])

config = %{
  dolphin_path: Path.expand(dolphin),
  iso_path: Path.expand(iso),
  character: :mewtwo,
  dummy_character:
    cond do
      String.starts_with?(kind, "absorb") -> :ness
      String.starts_with?(kind, "missile") -> :samus
      true -> :falco
    end,
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
  slippi_port: 51950 + Enum.find_index(kinds, &(&1 == kind)),
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

          absorb = String.starts_with?(kind, "absorb")
          missile = String.starts_with?(kind, "missile")
          finish = if absorb, do: 1300, else: 900

          p1 =
            cond do
              f >= finish ->
                input.(0.0, 0.5, %{})

              absorb and f >= 0 and f < 100 and gs.players[1].x < -25 ->
                input.(0.65, 0.5, %{})

              absorb and f in [120, 180, 360, 420, 650, 1050] ->
                input.(0.5, 0.5, %{b: true})

              absorb ->
                neutral

              kind == "reflect_control" or kind == "missile_control" ->
                neutral

              missile and f >= 120 and
                  rem(f - 120, 180) == if(kind == "missile_early", do: 40, else: 65) ->
                input.(1.0, 0.5, %{b: true})

              not missile and f >= 120 and rem(f - 120, 90) == 20 ->
                input.(1.0, 0.5, %{b: true})

              true ->
                neutral
            end

          p2 =
            cond do
              f >= finish ->
                neutral

              absorb and f >= 0 and f < 100 and gs.players[2].x > 25 ->
                input.(0.35, 0.5, %{})

              kind == "absorb" and (f in 350..600 or f in 900..1250) ->
                input.(0.5, 0.0, %{b: true})

              absorb ->
                neutral

              missile and f >= 120 and rem(f - 120, 180) == 0 ->
                input.(0.0, 0.5, %{b: true})

              not missile and f >= 120 and rem(f - 120, 90) == 0 ->
                input.(0.5, 0.5, %{b: true})

              true ->
                neutral
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
