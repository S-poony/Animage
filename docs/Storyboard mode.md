# What is a Storyboard ?

**The implementation of Storyboards should avoid modifying what a Scene is.**

- A storyboard is a different kind of folder as the .animage folders. 
	- It is .scénarimage (.scenarimage?)
	- Contains normal .animage scenes
		- You can add a .animage to a .scenarimage, and the new scene will appear in your storyboard.
		- The storyboard adds a prefix with its scene and sequence number to the scene's name.
	- Allows you to organize multiple scenes together, and quickly jump from one scene to another.
		- The unit of the storyboard is the Sequence. One sequence contains multiple scenes.
			- unlike drawings being on multiple tracks in scenes, sequences are all in one track, like the one reel of a movie.
			- There canot be empty space in a sequence. Removing a scene is similar to ripple delete in editing softwares.
			- Folders containing files is an incorrect analogy to sequences containing scenes. There cannot be a sequence inside a sequence, and there cannot be an empty sequence. 
				- By default, every scene is in its own sequence (I will use "[]" to represent sequences).
					- If you have [A], [B] and you drag [B] to [A], the result is [A, B]
		- additional import tracks do exist for audio, which can be across multiple scenes or sequences. No image or image sequence can be imported in storyboard mode
			- Not a priority but subtitle tracks would be a nice addition

# What can you do in a Storyboard ?

**These modifications to scenes are non destructive, and stored inside the storyboard. They don't touch the scenes directly**

- **Switch between scene and storyboard mode instantly, for any scene** 
	- Scene mode is what the app already does with .animage objects, basically what I want is to be able to edit the animation at any point of the storyboard. This may be the fundamental difference between storyboard mode and an editing software.
- New Scene
- Remove scene
	- It is deactivated and does not take space on the timeline, but its order is preserved and it can be reactivated at any time, where it will regain its space (by pushing the following scenes to the right) 
- Duplicate scene
- Reorder scenes
- Trim scenes
	- it may be useful to utilize/refactor the machinery we already have for trimming audio strips. 
- Join sequences 
- Separate sequences
	- if you have sequence [A,B,C] and you click "Separate sequence", the result is sequence [A] and sequence [B,C]
- Split a Scene
	- splits at the playhead's position

## Camera and FX

**This part of the plan involves building an keying system for the user, see section below for keying**

**Storyboard mode should not lag. Playback in storyboard mode should especially not lag. Unlike scene mode, image quality compromises to meet these requirements are acceptable.**

- Edit and key a Sequence's Camera
	- Camera properties:
		- Translate X
		- Translate Y
		- Zoom (=uniform scale)
- Edit and key a Sequence's FX
	- FX properties:
		- Crossfade with next sequence
		- Fade in to color
		- Fade out to color
			- The color is changeable by the user but non keyable I think. Default is black.
		- Blur (This one is not a priority, and is more complex than the others.)
			- Ideally, you should be able to select the range of the blur so it only affects the first layers or the last layers of the sequences scenes. This is tricky as several scenes in a sequence may have different number of layers

### Keying system

- Since there is only one track for the sequences (which means even with the audio, we will have a lot of row space in the timeline), We can adopt the After Effects approach to visualize keys, with one row for each property
	- Motion graphs are not the priority, as it is fine if motion is a bit rough in a storyboard. A way to Ease in and Ease out between two keys would be nice, but not a priority

# Export

- Video export is a must, luckily QTmultimedia already paves the way.
	- an option to watermark the sequence and scene prefix on the video would be great.
- A nice to have addition would be keyframe export (in whatever format is simplest, but best would be .pdf). Keyframe does not export every single frame but every unique scene drawing and maybe makes (bigger?) frames with camera in and out for camera moves. Basically the goal is to export the minimal amount of frames to give all the information of the video in a static form, writing text next to the frames is allowed.
# Uncertainties

- The "no empty space in a sequence" rule
	- I think it may be simpler for the code/to build on top off, and forces the user to adopt a rigorous mindset, makes it easier to switch the place of 2 scenes... but maybe it's just annoying
- what happens if scenes have different framerates/formats/resolution ?
- how the storyboard keeps track of scenes
- A panel for Camera, a panel for FX or one panel for both Camera and FX ?
- A sequence Panel that lets you reorder scenes and sequences more easily than the timeline alone ? The "there cannot be empty space" property would work well with this, all changes you make from the panel would be reflected in the timeline. This is what I had in mind when I described what is a sequence in [# What is a Storyboard]
- How the switching from storyboard to scene mode is reflected in the UI.
	- More precisely, I don't think storyboard mode should add any buttons to the UI (apart from file>new storyboard) when a scene is not opened from a storyboard. But this may not be elegant to implement.
	- Additional buttons in a scene opened from a storyboard could include next/previous scene