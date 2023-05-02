# GTR Framework

**NIA:** 217721 <br>
**Name:** Àlex Giménez Saludes <br>
**Mail:** alex.gimenez02@estudiant.upf.edu <br>

## How to use

Open the visual studio solution and run directly or, if you do not have any tool to start a visual studio solution, boot up the GTR.exe file
<br>
This delivery contains the implementation of lights (POINT, SPOT and DIRECTIONAL) with the multipass and singlepass methods. <br>Additionally, you can swap between different Render Modes with an ImGui Combo, the three modes are: FLAT, TEXTURED and LIGHTS. When selecting lights, 2 extra Widgets will appear:<br>

- A Switch toggle that changes between singlepass and multipass (Default: Multipass)
- A Checkbox that enables the visualization of shadowmaps (in light order, first Spot, then Point and then Directional)

<br> <br>

### Missing parts

The shadowmaps for point lights is missing due to not being the evaluation section of the [instructions](https://docs.google.com/presentation/d/1V4LeBHyZtTfkvAGaeogu9WFVzOJjWMuVk72FD_vd97Q/edit#slide=id.g845a05d37c_2_0).

### Some known issues

The shadows for singlepass do not work correctly, could not find why, therefore when entering singlepass, a new Switch will appear with the text: Enable Shadows
