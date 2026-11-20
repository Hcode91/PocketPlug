package com.hcode91.pocketplug

/**
 * The games inside the multi-game units, as their own menus list them
 * (read off the menus in the emulator). Lines starting "# " are menu sections.
 */
object GameLists {
    class Entry(val games: List<String>, val note: String, val numbered: Boolean = true)

    private val cache = HashMap<String, Entry?>()

    fun forMachine(id: String): Entry? = cache.getOrPut(id) { build(id) }

    private fun build(id: String): Entry? = when (id) {
        "ablmini" -> Entry(lines("""
Desert Strom
F22
Little Blackmask
Galaxy Killer
Five Kids
Super Fight1
Highway Racing
Overspeed Racing
Tank
Square
Boxes World
Table Tennis
Shooting Ballons
Diamond
Archer
Baseball
Xmas Gift
Trooper
Water Fire
Cat vs Dog
Pop Monster
Fireman
Stub Game
Danger Zone
Final Man
Corridor
Papa Zuma
Hot Speed
Soybeanstory
Magical Kitchen
Access Block
Germ Killer
Under Water Rescue
Heli Mission
Maze Puzzle
Battlefield
Monster War
Apple Chess
Dodgeball
HQ Fighter
Car Park
ET
Fishing Super
Magic Doors
Mars Worm
Master Chess
Maze Treasure
Connect Pipe
Super Tank
UFO Capture
Horrible Area
Warship
Hitting Mice
Snakes
Lawn Mower
Beetle
Plush Dog
Sea Man
Sur LC
Shoot
Radish Field
Chain Star
Undersea
Play Dice
Hurry
Orchard
Attacking
Volleyball
M Day
Dig Mines
Five Days
Robot
Airialhero
Starattack
Discus
2D Escape
Explorer
Clever Way
Ocean Prospect
Super Ward
"""), "")
        "88in1joy" -> Entry(lines("""
Fighter
Space War
Racing Car
Air Tiger
Pocky
Maze Ball
Magic Block
Street War
Hellfire
100 Meter Dash
Panzer Fly Car
Football
Winter Cup
Bumping R-16
Javelin Throw
Night Arrow
Van-1
Panda
Bicycle Race
High Jump
Scoring
Dragon
Long Jump
W W F L
Earth Guard
Ladangel
Hassle
Goodhand
Destroy1
Toto
Billiard
Combata
Super Bowl
110-M Hurdles
Beat Balloon
Cryptcar
Golgotha
Penguin
Roge Brer
Sky-Invader
Surface Fire
TNT
Smart Acel
Archery
Brush Roll 2
Aether Kavass
Bandits
Bounce
Conte Enegy
UFO Race
Mars-Man
Risker
Mars Climber
X Terk-2
Skeet Shooting
Gun Fight
Space War
Future Tank
Hoodle
Aether
Door Door
Bomb
Flying Bomber
Genius Chess
Triple Jump
Conqueror
Dada
Golfer
Mars
Maze
Rescue Kuck
Coin Tetris
UU Weapon
Gear Master
Astro Final
Monster
Shooter
Space 2050
Tennis
Spar
"""), "The menu lists 80 games (the box says 88).")
        "ablwikid" -> Entry(lines("""
Space War
High Tide
Hurricane
Air Force
Alien Attack
Frontier Fire
Bee's Attack
Shark
Ski
Road Hero
Future Soldier
Rescue
99 Layers
Balloon War
"""), "")
        "168pcase" -> Entry(lines("""
Contra
Panda Adventure
Tetris
Wizzy
Aether
Aether Kavass
Aimless
Radish Field
Angel
Antarctic Adventure
Arabian
Archery
Arena
Beankids
Enchanter
Bandits
Battle City
Battle City2
Billiard
Gold Digger
Bird Week
Bitha
Black White Chess
Bomb
Bomber Man
Bomb Sweeper
Bounce
Brush Roller
Bubble
Snow Field
Bug's War
Burbles
Burger Time
Burrow Explorer
Chack'n Pop
Circus Charlie
City Connection
Man in Red
Cobra of Sky
Cute Fish
Dada
Defender2
Deflower
Depth Bomb
Jungle Contra
Radar Bus
Devil Rider
Mine
Diamond
Dig Dug
Dinosaur
Discus Put
Care Bear
Fruitdish
110M Hurdles
Hangar
Bomb King
Awful Rushing
Dune War
Eggs
Goal Keeper
Excitebike
Exerion
High Jump
Combat
Fighter
Loop Gun
Lock On
Rope Games
Fire Base
Fish War
Formation Z
Front Line
Galaga
Galaxian
Gallagant
Gomoku
Hallihoo
Hit Mouse
Hoodle
Energy Zone
Unite War
Hot Speed
100M Dush
Ikki
IQ Champion
Javelin
Joust
Stub Game
Levin Action
Shot Put
Lode Runner
Lode Runner2
Shot Gun
Lunar Ball
Macross
Mad Xmas
Magic Bubble
First Defender
Mappy
Pobble
Mars
Conte Enegy
Combata
Maze Maid
Base1 Contra
Allied Forces
Cross Fire
Millipede
Move Box
Nibbles
Night Arrow
Ninja Kun
Final Man
Othello
Conqueror
Penguin
Penta Base
Poker
Pooyan
Shoot
Puzzle
Pyramid
Aether Fighter
Rescue Kuck
Risker
Road Fighter
Robot
Waterfall
Field Combat
Circus Vemture
Shrew Mouse
Rudder
Santa Claus
Sea War
Silent Hunter
Sky Destroyer
Sky Invader
Freecell
Triple Jump
Space ET
Space War
Spar
Star Force
Star Gate
Surface Fire
Base2 Contra
Brush 2
Tactful Mokey
Tank A1990
Tennis
The Archer
Tip Moto
TNT
Twin Cards
Twin Copters
Twin Fish
UFO Race
Undersea Arena
Long Jump
Warpman
Warrior
Wrestle
X Racing
Yie Ar Kung Fu
Zippy Race
Alien's Lair
Clown
"""), "")
        "36pcase" -> Entry(lines("""
Super Contra
Super Mario Brothers
Tetrisa
Boom Man
Balloon Fight
Donkey Kong
Excitebike
Galaga
Spartan X
Arkanoid
Five Chess
Mahjang
Ice Climber
Pac Man
Mario Bros
Tennis
Circus
Pooyan
Battle City
F1 Race
Pin Ball
Soccer
Lode Runner
Nuts Milk
Olympic
1942
Macross
Star Force
Road Fighter
Badmiton
Sky Destroyer
Bomb Jack
Chinese Chess
Urban Champion
Mappy
Penguin Kun Wars
"""), "")
        "500in1hh" -> Entry(lines("""
Chitei Tanken
Angry Bird
Contra1
Super Contra 2
Jackal
Rush'n Attack
Mitsume ga Tooru
Snow Bros
Contra Force
Super Contra 6
Super Contra 7
Super Contra 8
Final Mission
Super Mario Bros
Super Mario Bros3
Super Mario 6
Super Mario 9
Super Mario 10
Super Mario 14
Super Mario 16
Dr Mario
Street Fighter 20
Kick Master
Mighty Final Figh
Street Fighter VI
Mortal Combat 4
Adventure Island
Adventure Island2
Adventure Island3
Gradius
Life Force
Ninja Turtles 1
Chip & Dale 1
Chip & Dale 2
Chip & Dale 3
Hot Blood Basketb
Hot Blood Story
Hot Blood High Sc
Double Dragon 1
Double Dragon 2
Double Dragon 3
Double Dragon 4
Tiny Toon Adventu
Ninja Gaidem 1
Goonies
Battle City
Tank A 1990
Tetris
Chinese Chess
Gomoku Narabe
Mahjong 2P
Mahjong 4P
Yie Ar Kung-Fu
Spartan X
1942
Macross
Circus Charlie
Bomber Man
Lode Runner
Lode Runner 2
Ice Climber
Donkey Kong
Donkey Kong Jr
Donkey Kong 3
Arkanoid
100M Dush
10-Yard Fight
110M Hurdles
Addition
Aether2
Aimless
Airial Hero
Air Umbrella
Angel
Angler Fish
Antarctic Adventu
Antiquarium
Toy Story
Super Arabian
Lido
Archery
Arena
Assart
Awful Rushing
Baccarat
Balloon Fight
Base Ball
Dragon
Bean Kids
Violent
Binary Land
Bird Week
Bitha
Black White Chess
UFO Race
Bomb
Bomb King
Bomb Sweeper
Bounce
Box World
Brush Roller
Bubble
Bugbear
Bug's War
Burbles
Burger Time
Burrow Explorer
Cannonade
Care Bear
Chack'n Pop
City Connection
Pac Pac Land
Close Quarters
Clu Clu Land
Coast Guard
Cobra of Sky
Gold Digger
Combata
Conqueror
Conte Enegy
Corridor
Crisis
Crypt Car
Crystals
Cub Adventure
Cute Fish
Dada
Defender 2
Defensive
Deflower
Denger Zone
Depth Bomb
Devildom Doom
Devil Rider
Devil World
Diamond
Diamonds
Dice
Dig Dug
Small Dinosaur
Discus Put
Dominoes
Donkey Kong Jr Math
Door Door
Dragon Den
Dune War
Eggs
Billiard
Enchanter
Excitebike
Exerion
F1 Race
Slalom
Field Combat
Airway
Final Man
Firebase
First Defender
Destroy I
Fish War
Five Days
Formation Z
Frantic Mouse
Freecell
Front Line
Fruit
Fruit Dish
Future Tank
Galaga
Galaxian
Gallagant
Garden War
Gate
Ghost Palace
Ghost Ship
Glommy Chess
Goal Keeper
Goblet Tower
Golf
Golfer
Grot Kid
Hallihoo
Pocky
Hexapod War
High Jump
Memory Test
Hit Mouse
Hoodle
Hot Speed
Hyper Olympic
Hyper Sports
Ikki
Inkfish
IQ Champion
Javelin
Jewelry
Joust
Jumping Kid
Jungly Guy
Karateka
Elevator Action
Space Invaders
Levin Action
Link
Jump Long
Lucky Time
Lunar Ball
Lunarian
Lunation
Mad Xmas
Magic Bubble
Magic Place
Man in Red
Mappy
Mario Bros
Mars
Mars Man
Maze
Maze Maid
Millipede
Miner
Motoboat
Move Box
Nibbles
Night Arrow
Ninja Kun
Numbers
Nuts Milk
Old Maid
Muscle
Pac-Man
Paigow
Panzer Attack
Pathway
Dream Way
Penguin
Penta Base
Pillar
Pinball
Plush Dog
Pobble
Poker
Polar Bat
Pooyan
Popeye
Word Puzzle
Puzzle
Pyramid
Radish Field
Raid on Bungelin
Rescue Kuck
Resistant
Risker
Road Fighter
Robot
Roge Brer
Rudder
Rural Goblin
Salver
Santa Claus
Scuba Hunt
Season Garden
Sea War
Shoot
Shot Put
Shrew Mouse
Sky Destroyer
Sky Invader
Small Mario
Snowball
Space ET
Space War
Spar
Star
Star Fighter
Star Force
Star Gate
Strafe
Stub Game
Surface Fire
Tactful
Tactful Mokey
Tennis
The Archer
The Farmer
TNT
Transmission
Triple Jump
Twin Boar
Twin Cards
Twin Copters
Twin Fish
Undersea Arena
Urban Champion
Warpman
Warrior
Way Out
Wrestle
X Racing
Zippy Race
"""), "The menu counts to 500: entries 301-500 repeat entries 101-300.")
        "240in1ar" -> Entry(lines("""
# Sports
VR Tennis
Penalty Shootout
Worldwide Top Scorer
Football Training
Table Soccer
Beat Brick
Goals
6-A-side Football
Header
Soccer Cup Final
Badminton
Ski
Basketball
Darts
Ice Hockey
Shadowbox
Hercules
Soccer Boy
Skateboard
Horse Race
Roads
Cricket
Golfer
Rugby Fight
Animal Tennis
Wrestling
Drift Racing
Motorcycles
Motor Racer
Dodge the Ball
Super Bowling
F1 Race
Autoboat Contest
Table Ball
Baseball
Shoot
Shotput
Discus
Goalkeeper
# Shooting
Galaga
Street Man
Super Shooter
Missiles
Duck Hunter
Penguins
Sky Fighter
Wave Tiger
Popo
Tiger Man
UFO
Water Fire
Spacewar
Bug Fight
Shelters
Tanks
Magmax
Space Fighter
Fighter Plane
Enemy Attacker
Ship Defender
Seicross
Radar
Air Buster
Maze Arrow
Troopers
Bomb Release
Earth Defender
Trek
Sky Walker
Air Socker
Star Soldier
Battle Field
Battle Field 2
Galaxy War
Sea Fighter
Space War 2
Galaxy War 2
Shuttle Attack
Defender
Mighty Boys
Enemy Hunt
Tank
Trooper
Shooting Ballons
Robot
Final Man
Archer
Close Quarters
Enchanter
Defensive
Dragon Den
Airial Hero
Ghost Palace
Final Fighter
# Puzzle
Sudoku
Unwonted
Gem Copy
Fruit Couping
Rabbit
Pipes
Crazy Coins
Copy It
Twin Cards
Find Different
Western Restaurant
Germ Killer
Painting
Route
Match Up
Math Genius
Harry
Carpark
Picture Matcher
Picture Magic
Push Box
Ball Switch
Zoology
Magic Ball
Photo Puzzle
Maze
Speed Type
Stainless Glass
Ball Eliminator
Lets Count
Loop Legend
Tent
Bead Switch
Lode Runner
Math Genius
Match Up
Number Puzzle
Bead
Pyramid
Secret Mission
Fling Ball
Sum It Up
Pattern Puzzle
Box Jump
Color Balls
Constellation
Fire
Tryst
Genius
Gluttonous Snake
Smart Girl
Pillar Break
Lucky Ball
Dice
Backgammon
Maze Link
Balloon Chess
Cards
Chess
Convection
Battlefield
Toy Factory
Horrible Area
Lawn Purge
Undersea
Burrow
Panda
Tactful
Gem
# Arcade
Whack Mole
Music Composer
Blue Jack
Shepherd
Dual Hoopers
China Corpse
Fatso Jumper
Cat vs Dog
Magical Kitchen
Build Man
Duel
Helli Mission
Squirrel Courtship
Maggie
Iceberg
Farm Keeper
Cross River
Terminal
Fruit Eater
Exit
Fruit Collector
Abscondee
Smart Bomber
Gluttonous Dog
Jump Mouse
Maze Bomber
Car Treasure
Wall Breaker
Worms
BeeSting
Jungle Max 1
Birds
Survival
Combat Cars
Balloon Warrior
Balloon Zone
Wing Fighter
Falling
Maze Adventure
Street War
Sword Combat
Rescue the Girl
Tele Port
Gold Finder
Pro Sweeper
Ghost Adventure
Underground Battle
Candy Eater
Rabbit Girl
Wrecking
Acrobatics
Arabian
Robbers
Diamonds
Train Saver
Bonaza Bounce
Bee Jumper
Ice Climb
Key Finder
Ninja Stars
Climb Up
Ninjas
Underground
Maze Tpooper
Boat Race
Sqoon
Exist
Brave Boy
Cute Fish
Diamond
Plush
Blobman
Penguin
Eating
Fireman
Hit Mouse
Rocketman
"""), "")
        "265games" -> Entry(lines("""
# Main menu
Goal Keeper
Basketball
X-Racer III
Sky Hawk
Bubble
# Racing
Final Race
Motor Race
Thunder Race
Speed Motorboat
Malibu
Speed Race
Boat Race
Dash Motor
Super Skiing
Super Motor
Super F1 Race
Fish Journey
Deformable Car
Lightning
Racing Car
# IQ
Puzzle
Switchman
Grass Cutter
Snake
Bomb Kid
Tower Ladder
Railway Builder
Pipe Worker
Number Memory
Photo Match
Sky Up
Magic Key
Cross Street
Mine
# Sport
Bowling
Soccer Training
Shooting
Dart
Ring Game
Super Fishing
Soccer
Balance Walk
Hula Hoop
Curling
Archery
High Jump
Long Jump
Triple Jump
# Action
Heroic Pilot
Undersea War
Tank Tank
Space Defence
Base Defense
Horrific Collapsar
Space Warrior
Super Archer
Battle Tank
Earth Defence
Helicopter
Space Attack
Space Defend
The Last Fortress
# Adventure
Adventurer
Night Shooter
Riding Hunt
Hunting
Street Shooting
Brave Boy
Battleship
Beach War
Trooper
Final Blood
Cat's Guide
U Force
Kid Adventure
Bailout Blocks
"""), "The menu holds 76 games (the box counts 265): five on the main carousel, the rest in the 5 Game Zones.")
        "a6plus" -> Entry(lines("""
1942
3 Eye
Adventure Island 1
Adventure Island 2
Adventure Island 3
Adventure Island 4
Angry Bird 2
Angry Bird 3
Double Dragon 1
Double Dragon 2
Double Dragon 3
Double Dragon 4
Banana Prince
Batman
Battle City
Battle Formula
Battle Tank
Beach Volleyball
Billiard
Blue Shadow
Contra Force
Probotector II
RAF World J
Super Contra 1 12in1
Super Contra 2
Super Contra 2 20in1
Super Contra 2 24in1
Super Contra 6
Super Contra 7
Super Contra 8
Super Contra J
Tank H 1990
Boy Soccr
Bomber Man
Bubble Bobble Park 2
Macross
Circus Charlie
Dead Fox
F-1 Race
FC Genjin J
Galaga
Galaga Plus
Galaxian
Chip'n Dale 1
Chip'n Dale 2
Gold Digger
Golden Axe IV
Kagerou Densetsu J
Gradius
Greeds
Parodius
Gyrodine
Karateka
Hiryuu no Ken 3 J
Juuouki J
Kick Master
Legend of Kage
Lode Runner
Lode Runner 2
Metal Max J
Mickey Mouse
Mickey Mouse III J
Mickeys Safari in Num
Mighty Final Fight
Road Fighter
Mortal Kombat 4
Nekketsu Crashboy
Nekketsu Ice Hockey
Nekketsu Jidaigeki
Nekketsu Kakutou Dens
Nekketsu Koushinkyoku
Nekketsu Monogatari J
Nekketsu Soccer J
Nekketsu Street Baske
Ninja Gaiden 1
Ninja Gaiden 2
Ninja Gaiden 3
Ninja Turtles 1
Ninja Turtles 2
Ninja Turtles 3
Megaman III
Megaman V
Plants vs Zomb
Street Fighter 12
Street Fighter 2010
Side Pocket J
Snow Bros
Tank A 1990
Spartan X
Spartan X 2 J
Spider-Man
Spiderman 2
Spiderman 3
Super Chinese
Super Chinese 2
Super Shinobi
Tiny Toon 3
Tiny Toon Adv.
Tiny Toon Adv. 2
Viper
Yie Ar Kung-Fu
Warehouse Boy
100M Dush
110M Hurdles
Caveman Game
10-Yard Fight
1999 J
Chipmunks II
4 Mahjong
8 Eyes
80 Days
Abarenbou Tengu
Aces Iron Eagle III
Adventure of Rocky
Adventurer
Adventures of Lolo
Adventures of Lolo 2
Aerial Hero
Aerial Warfare
Aether
Aether Fighter
Aether Kavass
Aether Mission
Afro Man
Aim Cruise
Dora Bros
Air Alert 1
Air Raider
Air Umbrella
Air Wolf
Airial Hero
Airway
Akuma no Shoutaijou J
Aladdin
Aladdin III
Alice Cooper
Alien 3
Alienis
Altered Beast
Dracula
Animal Blockes
Antarctic Adventure
Arabian
Arena
Ares
Argus
Arkanoid
Asmik Kun Land J
Assart
Astro Fang
Astyanax
Atlantic Fishing
Babel no Tou
Baccarat
Backgammon
Backstroke
Dragon
Badminton
Bakushou Ai no Gekijo
Dorae Bomb
Baltron
Bananan Ouji no Daibo
Bandits
Barbie
Bart vs the Space Mut
Bart vs. the World
Bartman
Base Wars
Baseball Fighter
Baseball Stars
Baseball Stars II
Bases Loaded 3
Bases Loaded 4
Basketball
Batman Flash
Battle Beasts
Battle Field
Bean Kids
Bear vs Thief
Bicycle
Binary Land
Bionic Tank
Bird Week
Bitha
Black White Chess
Blackjack
Blaster
Blaster Master
Blink Ultra Hard
Blink3
Blob Buster
Blob Muncher
Block Puzzle
Blodia Land
Blue Marlin
Boat Race 2
Bokosuka Wars
Bolt Action
Bomb
Bomb Sweeper
Bonk's Adventure
Booger Man
Boomman
Bounce
Boxing Wrestle
Brave Boy
Break Man III
Breast Stroke
Brush Roller
Bubbles Puzzle
Bucky O'Hare
Bug
Bugbear
Burai Fighter
Burger Time
Burrow Explorer
Butterfly Stroke
B-Wings
Calculate
Calculator
Candy Workshop
Cannonade
Capcom Barcelona92
Caper Alone
Captain America
Captain Planet
Captain Tsubasa Vol 2
Cardg
Care Bear
Castle Quest J
Castle Vania II
Caveman Ninja
Chack'n Pop
Challenger
Chase H.Q.
Checkers
Chef Adventure
Chess
Chexdes
Chiki Chiki Machine M
Chronicle of the Radi
Chu Da D
Chubby Cherub
Circle Racing
City Connection
Cliff Hanger
Close Quarters
Clown's Mission
Clzlcn
Cobra of Sky
Colcotha
Colliding
Columbus J
Combata
Conqueror
Conquest of the Cryst
Conte Enegy
Contest 2004
Convection
Corridor
Cowboy Kid
Crazy Eights
Cricket
Cross Fire
Crypt Car
Cute Fish
Cyber Ball
Dada
Daiku no Gen San J
Dangerbridge
Dark Castle
Darkwing Duck
Defender II
Defenders of Dynatron
Deflower
Demogorgon Monk
Depth Bomb
Darkman
Destroy I
Destructive Dan 3
Devil Rider
Dough Boy
Difference
Dig Dug
Dig Dug II
Ding Dong
Discus Put
DJBoy
Dominoes
Donald Magic
Dirty Harry
Dragon Spirit
Dragon Strike
Dragons Lair
Dream Master
Duck
Duck-II
Days of Thunder
Earth Worm Jim 3
Eating
Egg Contest
Eidolon's Revenge
Eigo Asobi J
Elevator Action
Empire Strikes Back
English
Eon Man
Escape from Base 57
Escapeway
Europesensen
Dragon Den
Kachiuma Densetsu J
Exed Exes
Exerion
Exist
Explorer
Extreme Rally
F1 Circus
F-1 Hero 2 J
F-1 Hero J
F-1 Sensation
F-117A
F-15 Strike Eagle
F22
Falling Fruit
Family Boxing J
Family Circuit J
Family Jockey J
Family Mahjong J
Family Mahjong2 J
Family Pinball J
Family Stadium87
Family Tennis J
Famista'89 Kaimaku Ba
Fantasy Zone
Fatal Fury Special
Felix the Cat
Felix the Cat 2
Field Combat
FIFA 2002
FIFA 2004
FIFA 2005
FIFA-96 J
FIFA-99
Fighter
Fighting Road J
Final Fighter
Final Mission
Finaly Land
Fire Bird
Fire Bird J
Fire Dragon
Fire'n Ice
Fish War
Fishing
Fishwar
Kage
Flappy
Flappy Jack
Flipull
Forest Kid
Formation Z
Frantic Fishing
Free Style
Freecell
Front Line
Frontline Gallop
Fruit Gift
Fuel
Future Tank
Fuzzical Fighter
Galaxy 5000
Galaxy Patrol
Galg
Gargoyle's Quest II
Gate
Gauntlet
Gauntlet2
Geimos
Gemgame
Genpei Toumaden J
Ghost Palace
Ghost Ship
Giabbit
Gin Rummy
Glommy Chess
Goal-3
Gobang
Goblet Tower
Gofish
Gold Medal Challenge
Golgo13
Gun-Dec
Goonies
Gradirs
Grand Startegy
Grot Kid
Guerilla War
Gulf War
Gun Nac
Gundam J
Gundec J
Hacker
Karnov J
Happy Birthday Bugs
Happy Match
Harry Potter
Hassle
Heavy Barrel
Helicopter
Hell
Hello Kitty
Hen Sons
Hexa
Hexapod
Hexapod War
High Card
High Jump
Highway Racing I
George Foreman's KO B
Hissatsu Shigoto Nin
Hitting Mices
Home Alone 1
Home Alone 2
Hoodle
Hook
Huarongdao2
Hunt for Red October
Hydlide
Hyper Olympic
Hyper Sports
Mahjong Taisen
Mahjong3
Igo Meikan
Igo Shinan
Ikki
Image Fight
Indiana Jones
IQ Champion
Irontank
Isolated Warrior
J League Fighting Soc
J League Winning Goal
Jack Chan J
James Bond Jr
Javelin Throw
Jewelry
Jeweltetris
Joe Mac
Joust
Joy Mecha Fight J
Jumping Kid 2
Jumpin'Kid J
Jump-Jump
Jungle Book
Jurassic Boy
Jurassic Park
Jurassic Park the Los
Kabuki
Kabuki J
Kawa no Nushi Tsuri J
Kero Kero Keroppi
Kero Kero Keroppi 2
Kero Kero Keroppi 2 J
Kicking Now
Kickle Cubicle
Kid Klown
King's Quest V
Kinnikuman J
Maluex J
Kiwi Kraze
Klax
Knight Rider
Knock Time
Krusty's Fun House
Kun Land
Mappy
Kyatto Ninden Teyande
Kyouryuu Sentai Juura
Kyuutiger J
Ladangel
Lair's Secret
Lan Master
Last Action Hero
Lattice Winner
Lawn Purge
Le Comte de Monte Cri
Legacy of the Wizard
Levin Action
Little Krishna
Little Magic J
Little Ninja Brothers
Little Samson
Live in Power Bowl J
Lone Ranger
Long Jump
Lot Lot
Lowgman
Lucky Ball
Lucky Time
Luigi and Christmas Q
Luigi and New Quest
Lunar Pool
Lunarian
Lupin Sansei
Mappy-Land
Poker
Machine Mou Race
Chou Jikuu Yousai J
Mad Max
Mad Xmas
Mafat Conspiracy
Magic Aldoropie J
Magic Block
Magic Candle J
Magic Jony
Magician
Magmax
Poker AV
Mahjong Clear
Polar Bat
Polk
Pong
Mars
Mars Man
Masked Man
Matendouji
Maze
Mazemaid
MC Kids
Mickey in Letterland
Mendel Palace
Metro Cross
Metro Mania
Michael Andretti's Wo
Might and Magic
Mighty Final Fight J
Millipede
Miner 1
Mission Impossible
Mo Dao Shi Yin Mou
Monkey King
Monster in My Pocket
Moon Crystal
Move Box
Ms. Pac-Man
Murder Club
Muscle
Mystery J
Nadia
Naitou J
Namco Tennis 86 J
Namco Tennis 87 J
Namco Tennis 88 J
Nascah Challenge
Nekketsu Crashboy J
Nekketsu Ice Hockey J
Nekketsu Jidaigeki J
Nekketsu Kakutou Dens
Infiltrator
Nekketsu Soccer Leagu
Nekketsu Street Baske
Nemo Pajama Hero
Newzealand Story
Night Arrow
Night Shade
Ninja Fighter
Ninja Hanam
Ninja Hattori Kun
Ninja Kun
Ninja Ryukenden 2 J
Ninja Ryukenden 3 J
Noahs Ark
North South
Novice
Nuts Milk
Nyankies J
Obake no Q Tarou J
Old Maid
One by One
Onyanko Town J
Optimus Minor
Orchard Kavass
Othello
Othello 2
Over Horizon
Overlord
P.O.W Prisoners of Wa
Pac Man
Pac Man III
Pac Pac Land
Pachi Com
Pachiokun 3
Pachiokun 5
Pachislot Adventure 2
Pac-Land
Pac-Mania
Pai Gow Poker
Pooyan
Panic Restaurant
Panzer Fly Car
Paper Boy
Para Shooter
Parallel World
Parasol Henbee J
Pathway
Penguin Kun Wars
Penguin Seal
Pi Nuo Cao De Fu Su
Pikachu5
Strafe
Pindable
Pka Chu
Planet Race
Play Dice
Plumber
Porter
Portopia Renzoku
Power Blade
Power Blade II
Power Boat
Power Ranger J
Power Rangers 2 J
Power Soccer
Primitive Man
Primitive Woman
Project Doom
Protoman
Protoman5
Pro-Wrestling
Pulver
Punisher
Puzzle
Pyramid
Q Bert
Quarth
Quest Forge
Quest of Ki
Quinty
Racea Merica
Racing Fighter
Rackets Rivals
Street Fighter V 20P
Radia War
Radish Field
Raid on Bungeling Bay
Rainbow Silkroad
Rampage
Rampart
Ranch Boy
Rapid
Rasaarui Shii Quest J
RB Chess
RBI Baseball 1
RBI Baseball 2
RBI Baseball 3
Red Ariimaa II J
Ren Stimpy Show
Rescue Kuck
Rifle Range
Ring King
Risker
River City Ransom
River Jump
Road Worker
Robocco Wars J
Robocop
Robocop vs the Termin
Robocop2
Robocop4
Robot
Robot 2
Robot Block
Robot Gyro
Robot Maze
Rock'n'Ball
Rockin' Kats
Rockman 3 J
Rockman 5 J
Rockman X
Rocky and Bullwinkle
Roge Brer
Rollerblade Racer
Rollergames
Rolling Thunder
Roundball Challenge
Route16 Turbo
Rumble and Frenzy
Rush-up Boxing
Saint Seiya V
Sand Shot
Sanma no Mei Tantei J
Sansaara Naaga J
Sansuu Asobi J
Santa Claus
Sasa
Satsujin Club
Scoring
Scuba Hunt
SD Hero J
SD Sengoku Bushou Ret
Sea War
Seaman
Seaport Guarl
Secret Ties
Seething
Seicross
Seirei Densetsu Lickl
Shadow Gate
Shadow of the Ninja
Shadow Warriors II
Samurai
Shancara J
Shanghai Mahjong II J
Shatterhand
Shift
Shooting
Shootmath
Shot Put
Shounen Ashiba J
Shrew Mouse
Silent Hunter
Silk Worm
Silva Saga J
Skateboard Kid
Sky Destroyer
Sky Dreamer
Sky Invader
Sky Kid
Street Gangs
Slap Goblin
Slapjack
Slots
Smackdown
Small Dinosaur
Stub Game
Snaffo
Sniper
Snowball
Super Rescue J
Soda
Sodoku
Soldier
Solitaire 1
Solitaire 2
Solomon's no Kagi 2
Sonic
Sonson
Soreike! Anapanman J
Sky Wing
Space Invaders
Space War
Spades
Spar
Speed Challenge
Speed Racing
Spelunker
Splatter J
Spy vs Spy
Sqoon
Squeezer
Stanley
Star 1
Star 2
Star Force
Star Gate
Star Luster
Star Trek
Star Wars
Star Wars 2
Sub Attack
Sugoro Quest
Sugoro Quest J
Sui Tang
Summer Carnival 92
Sunman
Super Bowl
Super Fighter
Super Fighter 1
Super Hero
Target Practice
Teletubbies
The Flintstones 1
Tennis 2
Super Sprint
Super Spy Hunter
Super Tennis
Super Turrican
Super Xevious
Superman
Superstar Pro Wrestli
Surface Fire
Sword Master
Tactful Monkey
Tagteam Pro Wrestling
Taito Basketball
Talespin
Tank
Tennis Ball
Terminator
Terminator 2
Tetra Star
Tennis 3
Xia Ke Qing
Xmas Gift
Tetsuwan Atom
Texashold
The Archer
The Flintstones 2
The Farmer
The Last Ninja
The Legend of Khresna
Thexder
Three Country
Three Kingdom
Thrilla's Surfari
Thwaite
Tiger-He
Time Zone J
Tiny Toon Cartoon Wor
Titanic
TNT
Toad in the Hole
Todos Contra
Tom Sawyer no Bouken
Tom Jerry
Tom Jerry 3
Tomb Raider
Top Gun Dual Fighters
Topgun Second Mission
Totally Rad
Xmen2
Toxic Crusaders
Toy Bricks
Toy Factory
Toy Story
Trans-Mission
Triple Jump
Trolls in Crazyland
Trolls in Crazyland J
Trump Tank
Tunnel War
Tunny
Turtles Fighter
Turtles Fighter II
Turtles Fighter III
Twin Boar
Twin Cobra
Twin Copter
Twinbee
UFOracing
UFOuria
Ultimate Air Combat
Ultimate Basketball
Ultramanclub 2 J
Ultramanclub 3 J
Undersea
Uninvited
Upin Ipin Adventure
Chiteisenkuu J
US Championship V'Bal
Utmost Warfare
Utsurundesu J
UU Weapon
Valkyrie no Bouken J
Van 1
Vanguard
Video Player
Violent
Volguard II
Wacky Races
Wacky Races J
Wagan Land
Zen Intergalactic Nin
Wait and See
Wanpaku Kokkun J
War of Space
War Zone
Wario no Mori J
Zero Gravity
Warpman
Water Pipe
Wayne's World
Wayout
WCW Wrestling
Were Wolf
Western Kids J
Whale
Where's Waldo
Whomp Em
Widget
Wolverine
Wonder Ball
Wonder Rabbit
World Champ
Chiyo no Fuji J
World Cup 2002
World Cup Soccer
World Rally Champions
Worldcup
Castle Excellent
Wurm
X Racing
Xevious
YS2 Ancient YS Vanish
Yume Penguin Monogata
Zhan Guo Qun Xiong Zh
Zippy Race
WWF Steel Cage Challe
Zoids Mokushiroku J
Zombie Nation
Zoom
"""), "Names as the English menu shows them; \"J\" marks Japanese versions.")
        "afbm7800" -> Entry(lines("""
Adventure
Air Sea Battle
Asteroids
Battlezone
Breakout
Canyon Bomber
Centipede
Crystal Castles
Desert Falcon
Food Fight
Gravitar
Haunted House
Millipede
Planet Smashers
Saboteur
Sky Diver
Solaris
Sprintmaster
Warlords
Yars' Revenge
"""), "15 games first released on the 2600 and 5 on the 7800, ported to this console's NES-compatible chip.", numbered = false)
        "airblsjs" -> Entry(lines("""
Over the Cloud
Helicopter
Last Battle
Air Mission
F-15
"""), "")
        "abl4play" -> Entry(lines("""
Fortress Defender
Cannon Ball
Wizard Ball
Turn and Whack
Mountain Bike Rally
Soccer Maniac
Caterpillar
Christmas Surprise
Territory Pursuit
The Power Game
"""), "")
        else -> null
    }

    private fun lines(s: String) = s.split('\n').map { it.trim() }.filter { it.isNotEmpty() }
}
