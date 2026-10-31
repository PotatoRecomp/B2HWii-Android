using Translator.Core.Analysis.Representation;
using Translator.Core.Analysis.Ssa;
using Translator.Core.CodeGen;
using Translator.Core.Ir;
using Translator.Core.Representation;

namespace Translator.Tests;

// Guest memory range lowering runs inside Emit and rewrites runs of loads from one base into
// IrResolvedLoad / IrResolvedLoadPair. Every analysis that runs on the lowered body has to treat
// those forms as definitions exactly like IrLoad.
public sealed class ResolvedLoadPairedFlowTests
{
    private static RepresentationEnvironment FloatTypes() =>
        new(Enumerable.Range(0, 32)
            .ToDictionary(index => $"f{index}", _ => (ValueRepresentation)ValueRepresentation.Float64)
            .Concat(new Dictionary<string, ValueRepresentation>
            {
                ["r3"] = ValueRepresentation.UInt32,
                ["cr0"] = ValueRepresentation.UInt32
            })
            .ToDictionary(pair => pair.Key, pair => pair.Value));

    private static string Emit(IrFunction function, uint entry) =>
        new CxxLinearCodeGenerator().Emit(
            entry,
            new SsaTransformer().Convert(function),
            new FunctionAbiClassification(function.Name, ValueRepresentation.Void),
            FloatTypes());

    // Regression for the AHX voice noise (CRI DCT 803F8A9C): a range-resolved lfs inside a loop
    // kept the "paired" tag a non-entry block starts with, so its scalar consumer read the value
    // through PPC_PsToScalarInline (the high word of the double as a float).
    [Theory]
    [InlineData(false)] // interleaved: single IrResolvedLoad
    [InlineData(true)]  // adjacent: IrResolvedLoadPair
    public void ResolvedScalarFloatLoadsInLoopStayScalar(bool adjacent)
    {
        var loop = new List<IrInstruction>();
        if (adjacent)
        {
            for (var i = 0; i < 8; ++i)
                loop.Add(new IrLoad($"f{i + 1}", new IrAddress("r3", i * 4), 4));
            for (var i = 0; i < 8; ++i)
                loop.Add(new IrBinary("f9", IrValue.Register("f9"), IrValue.Register($"f{i + 1}"), "add"));
        }
        else
        {
            for (var i = 0; i < 8; ++i)
            {
                loop.Add(new IrLoad($"f{i + 1}", new IrAddress("r3", i * 8), 4));
                loop.Add(new IrBinary("f9", IrValue.Register("f9"), IrValue.Register($"f{i + 1}"), "add"));
            }
        }
        loop.Add(new IrBranch("beq", "loop", "exit"));

        var function = new IrFunction("resolved_lfs_loop", "entry", new[]
        {
            new IrBasicBlock("entry", new IrInstruction[] { new IrJump("loop") }),
            new IrBasicBlock("loop", loop.ToArray()),
            new IrBasicBlock("exit", new IrInstruction[]
            {
                new IrBinary("f10", IrValue.Register("f1"), IrValue.Register("f8"), "mul"),
                new IrReturn(null)
            })
        });

        var code = Emit(function, 0x8000A000);

        Assert.Contains("resolved", code, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("PPC_PsToScalarInline", code, StringComparison.Ordinal);
    }

    // The registered nonvolatile-FPR write mask is taken from the lowered body; a function whose
    // only f14-f31 writes are resolved loads must not be registered as preserving them.
    [Fact]
    public void ResolvedLoadsIntoNonvolatileFprsAreCountedAsWrites()
    {
        var body = new List<IrInstruction>();
        for (var i = 0; i < 8; ++i)
            body.Add(new IrLoad($"f{14 + i}", new IrAddress("r3", i * 8), 4));
        body.Add(new IrReturn(null));
        var function = new IrFunction("resolved_nonvolatile_fpr", "entry", new[]
        {
            new IrBasicBlock("entry", body.ToArray())
        });

        var code = Emit(function, 0x8000A100);

        Assert.Contains("resolved", code, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("preserves=false fpr_mask=0x003FC000", code, StringComparison.Ordinal);
    }
}
