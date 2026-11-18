local first = apo.generate {seed = '18446744073709551615', count = 3,
  min_transforms = 3, max_transforms = 4, variations = {'linear', 'swirl'}}
for i = first, first + 2 do
  apo.select(i)
  apo.mutate {seed = 70 + i, strength = 0.15, colors = true}
  print(i, apo.flame().name)
end
