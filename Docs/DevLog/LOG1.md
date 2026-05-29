# dev log 5/20/26

curantly the engine is at a nice place not that many features causing lag, it runs stabale 0 Scene at around 5000 fps, wich is not amazing but its works, and considering the 0 texture budget and no mesh system like nanite it was able to run a big 8K scene intended for 1 time blender rendering at a nice 244fps, dint try to push it.

whats next, the forward+ renderer is strong right now especality with the msaa, but deferred is laging behind, havent touch defered since i origianly added it. so that neads to be improved on. also i have not landed on what kinda of renderer i want like how much should be forward vs defered. 

for the renderer the next step is maby volumetics? i think thats like the bigest missing piece for the renderer other then like preformace stuff.

then its the part of user expernace, right now the editor looks awfull to the point where i who know how the engine works would rather just hard code the files, so i want to get it to a level where even i who is working on the engine will always use the editor just cus its faster then doing stuff in file.