// The scripting host: compiles Scripts/*.cs with Roslyn into an assembly of its own (a collectible load context,
// so a new one can replace it: hot reload), makes script instances for entities, runs them, and answers what
// classes and fields there are (for the Inspector). The engine calls in through ManagedApi (Bootstrap fills it).

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using Fox.Interop;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;

namespace Fox
{
    public static unsafe class Host
    {
        private sealed class ScriptContext : AssemblyLoadContext
        {
            public ScriptContext(int generation) : base("FoxScripts" + generation, isCollectible: true) { }

            // The scripts' references to the engine API resolve to the loaded one, not a copy
            protected override Assembly Load(AssemblyName name) =>
                name.Name == typeof(Script).Assembly.GetName().Name ? typeof(Script).Assembly : null;
        }

        private sealed class Instance
        {
            public long Id;
            public Entity Entity;
            public Script Script;
            public string ClassName;
            public bool Started;
            public bool Faulted;
        }

        private static string scriptsDirectory;
        private static ScriptContext context;
        private static int generation;
        private static Dictionary<string, Type> classes = new Dictionary<string, Type>();
        private static readonly SortedDictionary<long, Instance> instances = new SortedDictionary<long, Instance>();
        private static long nextInstance = 1;
        private static List<string> errors = new List<string>();
        private static List<MetadataReference> references;
        private static bool compiled;

        // --- Entry points ---

        [UnmanagedCallersOnly]
        public static int Bootstrap(ManagedApi* api)
        {
            api->Initialize = &Initialize;
            api->Reload = &Reload;
            api->CreateInstance = &CreateInstance;
            api->DestroyInstance = &DestroyInstance;
            api->Update = &Update;
            api->StopAll = &StopAll;
            api->GetInfo = &GetInfo;
            return 1;
        }

        [UnmanagedCallersOnly]
        private static int Initialize(NativeApi* api, byte* directory)
        {
            try
            {
                Native.Api = api;
                scriptsDirectory = Marshal.PtrToStringUTF8((IntPtr)directory);
                return Compile() ? 1 : 0;
            }
            catch (Exception e)
            {
                Native.Log(2, "Scripts: " + e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        private static int Reload()
        {
            try
            {
                return Compile() ? 1 : 0;
            }
            catch (Exception e)
            {
                Native.Log(2, "Scripts: " + e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        private static long CreateInstance(ulong entity, byte* className, byte* fieldsJson)
        {
            try
            {
                string name = Marshal.PtrToStringUTF8((IntPtr)className);
                if (!classes.TryGetValue(name ?? "", out Type type))
                {
                    Native.Log(2, $"Scripts: no script class \"{name}\" (on {new Entity(entity)}){(errors.Count > 0 ? ": the scripts do not compile" : "")}");
                    return 0;
                }
                var script = (Script)Activator.CreateInstance(type);
                script.Entity = new Entity(entity);
                ApplyFields(script, Marshal.PtrToStringUTF8((IntPtr)fieldsJson));
                var instance = new Instance { Id = nextInstance++, Entity = script.Entity, Script = script, ClassName = name };
                instances[instance.Id] = instance;
                return instance.Id;
            }
            catch (Exception e)
            {
                Native.Log(2, "Scripts: " + e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        private static void DestroyInstance(long id)
        {
            if (instances.Remove(id, out Instance instance) && instance.Started && !instance.Faulted)
            {
                Call(instance, "OnStop", () => instance.Script.OnStop());
            }
        }

        [UnmanagedCallersOnly]
        private static void Update(float deltaTime)
        {
            // A copy: scripts may add scripts (they start next frame) or destroy entities
            foreach (Instance instance in instances.Values.ToArray())
            {
                if (instance.Faulted || !instances.ContainsKey(instance.Id) || !instance.Entity.IsValid || !instance.Entity.Active)
                {
                    continue;
                }
                if (!instance.Started)
                {
                    instance.Started = true;
                    if (!Call(instance, "OnStart", () => instance.Script.OnStart())) continue;
                }
                Call(instance, "OnUpdate", () => instance.Script.OnUpdate(deltaTime));
            }
        }

        [UnmanagedCallersOnly]
        private static void StopAll()
        {
            foreach (Instance instance in instances.Values.ToArray())
            {
                if (instance.Started && !instance.Faulted)
                {
                    Call(instance, "OnStop", () => instance.Script.OnStop());
                }
            }
            instances.Clear();
        }

        // JSON for the Inspector: { "ok", "errors": [...], "classes": [ { "name", "fields": [ { "name", "type", "value" } ] } ] }.
        // Returns the length in bytes; writes it when it fits in capacity.
        [UnmanagedCallersOnly]
        private static int GetInfo(byte* buffer, int capacity)
        {
            try
            {
                using var stream = new MemoryStream();
                using (var json = new Utf8JsonWriter(stream))
                {
                    json.WriteStartObject();
                    json.WriteBoolean("ok", errors.Count == 0 && compiled);
                    json.WriteStartArray("errors");
                    foreach (string error in errors) json.WriteStringValue(error);
                    json.WriteEndArray();
                    json.WriteStartArray("classes");
                    foreach (var (name, type) in classes.OrderBy(c => c.Key))
                    {
                        json.WriteStartObject();
                        json.WriteString("name", name);
                        json.WriteStartArray("fields");
                        object defaults = null;
                        try { defaults = Activator.CreateInstance(type); } catch { }
                        foreach (FieldInfo field in EditableFields(type))
                        {
                            json.WriteStartObject();
                            json.WriteString("name", field.Name);
                            json.WriteString("type", TypeName(field.FieldType));
                            json.WriteString("value", defaults != null ? FormatValue(field.GetValue(defaults)) : "");
                            json.WriteEndObject();
                        }
                        json.WriteEndArray();
                        json.WriteEndObject();
                    }
                    json.WriteEndArray();
                    json.WriteEndObject();
                }
                byte[] bytes = stream.ToArray();
                if (bytes.Length <= capacity)
                {
                    Marshal.Copy(bytes, 0, (IntPtr)buffer, bytes.Length);
                }
                return bytes.Length;
            }
            catch (Exception e)
            {
                Native.Log(2, "Scripts: " + e);
                return 0;
            }
        }

        // --- Compiling ---

        private static bool Compile()
        {
            var watch = System.Diagnostics.Stopwatch.StartNew();
            string[] files = Directory.Exists(scriptsDirectory)
                ? Directory.GetFiles(scriptsDirectory, "*.cs", SearchOption.AllDirectories)
                    .Where(f => !f.Contains(Path.DirectorySeparatorChar + "obj" + Path.DirectorySeparatorChar)
                             && !f.Contains(Path.DirectorySeparatorChar + "bin" + Path.DirectorySeparatorChar))
                    .ToArray()
                : Array.Empty<string>();

            var parse = new CSharpParseOptions(LanguageVersion.Latest);
            var trees = new List<SyntaxTree>();
            foreach (string file in files)
            {
                string text = ReadShared(file);
                trees.Add(CSharpSyntaxTree.ParseText(text, parse, file, Encoding.UTF8));
            }

            references ??= ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES"))
                .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                .Select(path => (MetadataReference)MetadataReference.CreateFromFile(path))
                .Append(MetadataReference.CreateFromFile(typeof(Script).Assembly.Location))
                .ToList();

            generation++;
            var compilation = CSharpCompilation.Create("FoxScripts" + generation, trees, references,
                new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, optimizationLevel: OptimizationLevel.Debug,
                    nullableContextOptions: NullableContextOptions.Disable, allowUnsafe: true));

            using var dll = new MemoryStream();
            using var pdb = new MemoryStream();
            EmitResult result = compilation.Emit(dll, pdb, options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
            var newErrors = result.Diagnostics
                .Where(d => d.Severity == DiagnosticSeverity.Error)
                .Select(d =>
                {
                    var span = d.Location.GetLineSpan();
                    return $"{Path.GetFileName(span.Path)}({span.StartLinePosition.Line + 1},{span.StartLinePosition.Character + 1}): {d.GetMessage(CultureInfo.InvariantCulture)}";
                })
                .ToList();
            if (!result.Success)
            {
                errors = newErrors;
                foreach (string error in errors) Native.Log(2, "Scripts: " + error);
                Native.Log(2, compiled ? "Scripts: not reloaded (the running scripts stay as they were)" : "Scripts: none loaded");
                return false;
            }

            dll.Position = 0;
            pdb.Position = 0;
            var newContext = new ScriptContext(generation);
            Assembly assembly = newContext.LoadFromStream(dll, pdb);
            var newClasses = new Dictionary<string, Type>();
            foreach (Type type in assembly.GetTypes().Where(t => typeof(Script).IsAssignableFrom(t) && !t.IsAbstract))
            {
                newClasses[type.Name] = type;
            }

            // Running instances move to the new classes, keeping their fields
            int moved = 0;
            foreach (Instance instance in instances.Values.ToArray())
            {
                if (!newClasses.TryGetValue(instance.ClassName, out Type type))
                {
                    Native.Log(1, $"Scripts: {instance.ClassName} is gone: {instance.Entity} no longer runs it");
                    instances.Remove(instance.Id);
                    continue;
                }
                try
                {
                    var script = (Script)Activator.CreateInstance(type);
                    script.Entity = instance.Entity;
                    CopyFields(instance.Script, script);
                    instance.Script = script;
                    instance.Faulted = false;
                    moved++;
                    if (instance.Started) Call(instance, "OnReload", () => script.OnReload());
                }
                catch (Exception e)
                {
                    Native.Log(2, $"Scripts: {instance.ClassName} on {instance.Entity} could not be reloaded: {e.Message}");
                    instances.Remove(instance.Id);
                }
            }

            ScriptContext old = context;
            context = newContext;
            classes = newClasses;
            errors = newErrors;
            bool first = !compiled;
            compiled = true;
            old?.Unload();
            Native.Log(0, $"Scripts: {(first ? "compiled" : "reloaded")} {files.Length} file{(files.Length == 1 ? "" : "s")}, {classes.Count} script class{(classes.Count == 1 ? "" : "es")}"
                + (moved > 0 ? $", {moved} running script{(moved == 1 ? "" : "s")} carried over" : "") + $" ({watch.ElapsedMilliseconds} ms)");
            return true;
        }

        // The editor may still be writing the file: read it even so, and retry a moment later if it is locked
        private static string ReadShared(string path)
        {
            for (int attempt = 0; ; attempt++)
            {
                try
                {
                    using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var reader = new StreamReader(stream, Encoding.UTF8);
                    return reader.ReadToEnd();
                }
                catch (IOException) when (attempt < 10)
                {
                    System.Threading.Thread.Sleep(20);
                }
            }
        }

        // --- Fields ---

        private static readonly Type[] EditableTypes = { typeof(float), typeof(int), typeof(bool), typeof(string), typeof(Vector3), typeof(double) };

        private static IEnumerable<FieldInfo> EditableFields(Type type) =>
            type.GetFields(BindingFlags.Public | BindingFlags.Instance)
                .Where(f => !f.IsInitOnly && EditableTypes.Contains(f.FieldType));

        private static string TypeName(Type type) =>
            type == typeof(float) || type == typeof(double) ? "float" : type == typeof(int) ? "int" : type == typeof(bool) ? "bool"
            : type == typeof(Vector3) ? "vector3" : "string";

        private static string FormatValue(object value) => value switch
        {
            float f => f.ToString("R", CultureInfo.InvariantCulture),
            double d => d.ToString("R", CultureInfo.InvariantCulture),
            int i => i.ToString(CultureInfo.InvariantCulture),
            bool b => b ? "true" : "false",
            Vector3 v => string.Join(",", new[] { v.X, v.Y, v.Z }.Select(c => c.ToString("R", CultureInfo.InvariantCulture))),
            null => "",
            _ => value.ToString(),
        };

        private static object ParseValue(Type type, string text)
        {
            if (type == typeof(float)) return float.Parse(text, CultureInfo.InvariantCulture);
            if (type == typeof(double)) return double.Parse(text, CultureInfo.InvariantCulture);
            if (type == typeof(int)) return int.Parse(text, CultureInfo.InvariantCulture);
            if (type == typeof(bool)) return text == "true" || text == "1";
            if (type == typeof(Vector3))
            {
                float[] c = text.Split(',').Select(x => float.Parse(x, CultureInfo.InvariantCulture)).ToArray();
                return new Vector3(c[0], c[1], c[2]);
            }
            return text;
        }

        // The Inspector's values (a JSON object of name: text) over the class's defaults
        private static void ApplyFields(Script script, string json)
        {
            if (string.IsNullOrEmpty(json)) return;
            using JsonDocument document = JsonDocument.Parse(json);
            foreach (JsonProperty property in document.RootElement.EnumerateObject())
            {
                FieldInfo field = script.GetType().GetField(property.Name, BindingFlags.Public | BindingFlags.Instance);
                if (field == null || !EditableTypes.Contains(field.FieldType)) continue;
                try
                {
                    field.SetValue(script, ParseValue(field.FieldType, property.Value.GetString() ?? ""));
                }
                catch (Exception)
                {
                    Native.Log(1, $"Scripts: {script.GetType().Name}.{property.Name}: \"{property.Value}\" is not a {TypeName(field.FieldType)}");
                }
            }
        }

        // Hot reload: every field (public or not) of the same name whose type still fits; types defined by the
        // scripts themselves are new types now, so they start over
        private static void CopyFields(Script from, Script to)
        {
            const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly;
            for (Type type = from.GetType(); type != null && type != typeof(Script); type = type.BaseType)
            {
                foreach (FieldInfo field in type.GetFields(flags))
                {
                    FieldInfo target = null;
                    for (Type t = to.GetType(); t != null && t != typeof(Script) && target == null; t = t.BaseType)
                    {
                        target = t.GetField(field.Name, flags);
                    }
                    if (target == null || target.IsInitOnly) continue;
                    object value = field.GetValue(from);
                    if (value == null || target.FieldType.IsInstanceOfType(value))
                    {
                        target.SetValue(to, value);
                    }
                }
            }
        }

        // --- Running ---

        private static bool Call(Instance instance, string what, Action action)
        {
            try
            {
                action();
                return true;
            }
            catch (Exception e)
            {
                instance.Faulted = true; // Not called again until it is reloaded
                Exception inner = e is TargetInvocationException { InnerException: not null } ? e.InnerException : e;
                Native.Log(2, $"Scripts: {instance.ClassName}.{what} on {instance.Entity}: {inner.GetType().Name}: {inner.Message}\n{inner.StackTrace}\n"
                    + "  (it is stopped until its file is saved again)");
                return false;
            }
        }

        internal static T FindScript<T>(Entity entity) where T : Script
        {
            foreach (Instance instance in instances.Values)
            {
                if (instance.Entity == entity && instance.Script is T script) return script;
            }
            return null;
        }
    }
}
