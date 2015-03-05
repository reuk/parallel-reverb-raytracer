import saito.objloader.*;
import org.json.*;
import peasy.*;

OBJModel model;
PeasyCam cam;

class Intersection
{
  Intersection (PVector position, float volume)
  {
    _position = position;
    _volume = volume;
  }
  
  public PVector _position;
  public float _volume;
}

ArrayList <ArrayList <Intersection>> trace = new ArrayList <ArrayList <Intersection>>();

PVector source_pos;
PVector mic_pos;

void translate (PVector p) {translate (p.x, p.y, p.z);}

void setup()
{
  size (1000, 1000, P3D);
  colorMode (RGB, 1);
  frameRate (25);
  
  rotateZ(PI / 2);
  
  model = new OBJModel (this, "/Users/reuben/dev/parallel_raytrace/assets/test_models/large_square.obj");
  
  JSONObject config_json = loadJSONObject ("../assets/large_square.json");
  JSONArray s_pos = config_json.getJSONArray ("source_position");
  JSONArray m_pos = config_json.getJSONArray ("mic_position");
  
  source_pos = new PVector (s_pos.getFloat (0), -s_pos.getFloat (1), s_pos.getFloat (2));
  mic_pos = new PVector (m_pos.getFloat (0), -m_pos.getFloat (1), m_pos.getFloat (2));

  //BufferedReader reader = createReader ("/Users/reuben/Desktop/py_test_p.json");
  BufferedReader reader = createReader ("/Users/reuben/dev/parallel_raytrace/build/bin/impulse.dump");

  boolean read = true;
  while (read)
  {    
    try
    {
      String line = reader.readLine();
      JSON json = JSON.parse (line);
      ArrayList <Intersection> list = new ArrayList <Intersection>();
      list.add (new Intersection (source_pos, 1));
      for (int i = 0; i != json.length(); ++i)
      {
        JSON vec = json.getJSON (i).getJSON ("position");
        PVector pvec = new PVector (vec.getFloat (0), -vec.getFloat (1), vec.getFloat (2));
        list.add (new Intersection (pvec, json.getJSON (i).getFloat ("volume")));
      }

      trace.add (list);
    }
    catch (Exception e)
    {
      println (e);
      read = false;
    }
  }
  
  println (trace.size());
  
  perspective (1, width / float (height), 1, 1000);
  
  cam = new PeasyCam(this, 100);
  cam.setMinimumDistance(0);
  cam.setMaximumDistance(200);
}

void vertex (PVector p) 
{
  vertex (p.x, p.y, p.z);
}

void line (PVector a, PVector b)
{
  line (a.x, a.y, a.z, b.x, b.y, b.z);
}

void stroke (PVector c)
{
  stroke (c.x, c.y, c.z);
}

boolean doFill = false;

void mousePressed()
{
  doFill = !doFill;
}

void draw()
{
  lights();
  background (0.5);
//  scale (8);
  
  pushMatrix();
  fill (1, 0, 0);
  noStroke();
  translate (source_pos);
  sphere (1);
  popMatrix();
  
  pushMatrix();
  fill (0, 1, 1);
  noStroke();
  translate (mic_pos);
  sphere (1);
  popMatrix();

  if (doFill)
    fill (0.5);
  else
    noFill();
  
  PVector mini = new PVector();
  PVector maxi = new PVector();

  for (int i = 0; i != model.getSegmentCount(); ++i)
  {
    Face[] face = model.getSegment (i).getFaces();

    for (int j = 0; j != face.length; ++j)
    {
      PVector[] vertex = face [j].getVertices();

      stroke (0, 0, 0);

      beginShape();

      for (int k = 0; k != vertex.length; ++k)
      {        
        PVector temp = vertex [k].get();
//        temp = new PVector (temp.x, -temp.z, -temp.y);
          temp = new PVector (temp.x, temp.y, temp.z);
        
        mini.set (min (mini.x, temp.x), min (mini.y, temp.y), min (mini.z, temp.z));
        maxi.set (max (maxi.x, temp.x), max (maxi.y, temp.y), max (maxi.z, temp.z));
        vertex (temp);
      }

      endShape (CLOSE);
    }
  }
  
  println (mouseX);

  ArrayList <Intersection> list = (ArrayList <Intersection>) trace.get (mouseX);
  
  noFill();
  beginShape();
  for (int j = 0; j != list.size(); ++j)
  {
    if (((Intersection) list.get (j))._position.x != 0 || ((Intersection) list.get (j))._position.y != 0 || ((Intersection) list.get (j))._position.z != 0)
    {
      stroke (abs(((Intersection) list.get (j))._volume * 100000), 0, 0);
      vertex (((Intersection) list.get (j))._position);
    }
  }
  endShape();
    
//  saveFrame ("out-###.png");
}
