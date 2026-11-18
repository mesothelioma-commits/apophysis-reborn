-- Enable render requests and choose an output folder in the host.
for i = 1, apo.count() do
  apo.select(i)
  apo.render(string.format('study-%03d.png', i),
             {width = 640, height = 480, samples = 300000, seed = 91})
end
print('Prepared', apo.count(), 'render requests')
