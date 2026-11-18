-- Edit the current flame; all changes form one undo step.
apo.set_flame {name = 'Script study', gamma = 2.2}
for i = 1, apo.transform_count() do
  local t = apo.transform(i)
  t.color = (i - 1) / math.max(1, apo.transform_count() - 1)
  t.pre[5] = t.pre[5] + 0.08 * math.sin(i)
  apo.set_transform(i, t)
end
apo.palette_from_stops({{0,0.02,0.03,0.12}, {128,0.9,0.12,0.05}, {255,1,0.9,0.6}})
print('Edited', apo.transform_count(), 'transforms')
